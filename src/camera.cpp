//camera.cpp
#include "camera.h"
#include "debug.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

Camera::Camera(const char *dev, uint32_t memCount): memCount(memCount)
{ 
    devfd = open(dev, O_RDWR); //打开设备文件
    if (devfd == -1)
    {
        logp(dev);
        return;
    }
    //判断设备是不是视频设备
    struct v4l2_capability cap;
    if (ioctl(devfd, VIDIOC_QUERYCAP, &cap) == -1)
    {
        logp("<Camera>ioctl VIDIOC_QUERYCAP");
        return;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        loge("<Camera>%s is not a video device.\n", dev);
        return;        
    }
    memMaps = new MemInfo[memCount];
    //fmt
    memset(fmt.description, 0, sizeof(fmt.description));
    fmt.width = 0;
    fmt.height = 0;

    isstart = false;
    isloop = false;

    runbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    runbuf.memory = V4L2_MEMORY_MMAP;
}

Camera::~Camera()
{
    int index;
    isstart && stop();
    for(index = 0; index < memCount; index++)
    {
        if(munmap(memMaps[index].addr, memMaps[index].len))
        {
            logp("<~Camera>munmap");
        }
    }
    close(devfd);
    delete [] memMaps;
}

bool Camera::init(CameraFmt & cameraFmt)
{
    //查看摄像头是否支持指定格式
    struct v4l2_fmtdesc fmtd;
    int index, isFmt = 0;
    for (index = 0;; index++)
    {
        fmtd.index = index; //只有一个摄像头的设备
        fmtd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(devfd, VIDIOC_ENUM_FMT, &fmtd) == -1)
            break;
        if (fmtd.pixelformat == cameraFmt.pixelformat)
        {
            isFmt = 1;
            break;
        }
    }
    if (isFmt == 0)
    {
        loge("<Camera::init>camera does not support %s.\n", cameraFmt.description);
        return false;
    }
    //设置采集格式
    struct v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = cameraFmt.width;
    fmt.fmt.pix.height = cameraFmt.height;
    fmt.fmt.pix.pixelformat = cameraFmt.pixelformat; //YUYV
    if (ioctl(devfd, VIDIOC_S_FMT, &fmt) == -1)
    {
        logp("<Camera::init>ioctl VIDIOC_S_FMT");
        return false;
    }
    //申请采集缓存
    struct v4l2_requestbuffers reqbufs;
    reqbufs.count = memCount;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    if (ioctl(devfd, VIDIOC_REQBUFS, &reqbufs) == -1)
    {
        logp("<Camera::init>ioctl VIDIOC_REQBUFS");
        return false;
    }
    //mmap 将申请到的采集缓存映射到用户内存空间中
    struct v4l2_buffer buf;
    for (index = 0; index < memCount; index++)
    {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = index;
        if (ioctl(devfd, VIDIOC_QUERYBUF, &buf) == -1)
        {
            logp("<Camera::init>ioctl VIDIOC_QUERYBUF");
            return false;
        }
        memMaps[index].len = buf.length;
        memMaps[index].addr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, devfd, buf.m.offset);
        if (memMaps[index].addr == MAP_FAILED)
        {
            while(index--)
            {
                munmap(memMaps[index].addr, memMaps[index].len);
            }
            logp("<Camera::init>mmap");
            return false;
        }
        //把buf放入采集队列中
        if (ioctl(devfd, VIDIOC_QBUF, &buf) == -1)
        {
            while(index--)
            {
                munmap(memMaps[index].addr, memMaps[index].len);
            }
            logp("<Camera::init>ioctl VIDIOC_QBUF");
            return false;
        }
    }
    memcpy(&fmt, &cameraFmt, sizeof(CameraFmt));
    return true;
}

list<CameraFmt> *Camera::getSupportfmt()
{
    list<CameraFmt> *fmtlist = new list<CameraFmt>;
    int i, j, k;
    struct v4l2_fmtdesc fmtd;
	struct v4l2_frmsizeenum  frmsize;
	struct v4l2_frmivalenum  framival;
    CameraFmt tmp;
	for (i = 0; ; i++)
	{
		fmtd.index = i;
		fmtd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;		
		if (ioctl(devfd, VIDIOC_ENUM_FMT, &fmtd) == -1)
            break;
		// 查询这种图像数据格式下支持的分辨率
		for (j = 0; ; j++)
		{
            //tmp = new CameraFmt;
			frmsize.index = j;
			frmsize.pixel_format = fmtd.pixelformat;
			if (ioctl(devfd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == -1)
            {
                break;
            }
            strcpy(tmp.description, (char *)fmtd.description);
            tmp.pixelformat = fmtd.pixelformat;
            tmp.width = frmsize.discrete.width;
            tmp.height = frmsize.discrete.height;
            fmtlist->push_back(tmp);
        }
    }
    return fmtlist;
}

bool Camera::start()
{
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(devfd, VIDIOC_STREAMON, &type) == -1)
    {
        logp("<Camera::start>start failed");
        return false;
    }
    isstart = true;
    return true;
}

bool Camera::stop()
{
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(devfd, VIDIOC_STREAMOFF, &type) == -1)
    {
        logp("<Camera::stop>stop failed");
        return false;
    }
    isstart = false;
    isloop = false;
    return true;
}

void Camera::loop()
{
    bool ret;
    uint32_t len;
    if(!isstart)
    {
        uint8_t trytimes = 5;
        while(trytimes--)
        {
            ret = start();
            if(ret) break;
        }
        if(!ret)
        {
            loge("<Camera::loop>can't start camera.\n");
            return;
        }
    }
    isloop = true;
    while(isloop)
    {
        len = getFrame();
        if(len == -1)
        {
            loge("<Camera::loop>getData err.\n");
            break;
        }
        callback(&runbuf, &memMaps[runbuf.index], cb_tag);
        
        
    }
}

long Camera::getFrame()
{
    //出队采集好的数据，为采集好的阻塞
    if(ioctl(devfd, VIDIOC_DQBUF, &runbuf) == -1)
    {
        logp("<Camera::getFramer>ioctl VIDIOC_DQBUF");
        return -1;
    }
    return runbuf.bytesused;
}

bool Camera::returnFrame(uint32_t i)
{
    struct v4l2_buffer buf;
    buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory  = V4L2_MEMORY_MMAP;
    buf.index   = i;

    if(ioctl(devfd, VIDIOC_QBUF, &buf) == -1)
    {
        logp("<Camera::getFrame>ioctl VIDIOC_QBUF");
        return false;
    }
    return true;
}