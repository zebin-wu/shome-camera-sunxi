#include "camera.h"
#include "vencoder.h"
#include "debug.h"

#include <iostream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <algorithm>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <queue>

#define DEV_PATH "/dev/video0"
#define OUT_PATH "test.264"

static struct Venc_context {
    pthread_t    tid; //编码线程
    FILE         *out_file; //输出文件
    VideoEncoder *pEncoder; //编码器
    VencBaseConfig baseConfig; //编码器配置
    queue<VencInputBuffer *> inputQue; //input buffer queue
    //lock 
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} *venc_cxt;

static Camera *camera;

FILE *createOutFile(const char *path);
int yuv_422to420p(const uint8_t *src, uint8_t *dest_y, uint8_t *dest_c, uint32_t width, uint32_t height);

void sigint_cb(int signo)
{
    camera->disloop();
}
void sigal_init()
{
    signal(SIGINT, sigint_cb);
}

void *encode_thread(void *data)
{
    int ret;
    Venc_context *venc_cxt = ( Venc_context *)data;
    VencOutputBuffer out_buffer;
    while(!camera->isLoop());
    while(camera->isLoop())
    {
        pthread_mutex_lock(&venc_cxt->mutex);
        while(venc_cxt->inputQue.empty()) //保证队列为空时才进入
        {
            pthread_cond_wait(&venc_cxt->cond, &venc_cxt->mutex);
        }
        if(ret = VideoEncodeOneFrame(venc_cxt->pEncoder))
        {
            loge("encoder failed. err:%d\n", ret);
            pthread_mutex_unlock(&venc_cxt->mutex);
            continue;
        }
        VencInputBuffer *input_buffer = venc_cxt->inputQue.front();
        venc_cxt->inputQue.pop();
        AlreadyUsedInputBuffer(venc_cxt->pEncoder, input_buffer);
        ReturnOneAllocInputBuffer(venc_cxt->pEncoder, input_buffer);
        camera->returnFrame(input_buffer->nID);
        //解锁
        pthread_mutex_unlock(&venc_cxt->mutex);
        delete input_buffer;
        if(ret = GetOneBitstreamFrame(venc_cxt->pEncoder, &out_buffer))
        {
            loge("getStream err. err:%d\n", ret);
            continue;
        }
        fwrite(out_buffer.pData0, 1, out_buffer.nSize0, venc_cxt->out_file);
        if(out_buffer.nSize1)
        {
            fwrite(out_buffer.pData1, 1, out_buffer.nSize1, venc_cxt->out_file);
        }
        FreeOneBitStreamFrame(venc_cxt->pEncoder, &out_buffer);
    }
    logd("encoder end\n");
    return NULL;
}

void dealData(struct v4l2_buffer *runbuf, struct MemInfo *memMap, void *tag)
{
    if(!tag)    return;
    Venc_context *venc_cxt = (Venc_context *)tag;
    //将buf入队
    VencInputBuffer *input_buffer = new VencInputBuffer;
    GetOneAllocInputBuffer(venc_cxt->pEncoder, input_buffer);
    input_buffer->nID = runbuf->index;
    yuv_422to420p((uint8_t *)memMap->addr, input_buffer->pAddrVirY, input_buffer->pAddrVirC, 
                    venc_cxt->baseConfig.nInputWidth, venc_cxt->baseConfig.nInputHeight);
    FlushCacheAllocInputBuffer(venc_cxt->pEncoder, input_buffer);
    int ret = AddOneInputBuffer(venc_cxt->pEncoder, input_buffer);
    if(ret < 0)
    {
        loge("venc input buffer is full, skip this frame.\n");
    }
    //加入到input queue
    pthread_mutex_lock(&venc_cxt->mutex);
    venc_cxt->inputQue.push(input_buffer);
    pthread_mutex_unlock(&venc_cxt->mutex);
    pthread_cond_signal(&venc_cxt->cond);
}

int main()
{
    int ret;
    sigal_init();
    venc_cxt = new Venc_context;
    //lock init
    venc_cxt->mutex = PTHREAD_MUTEX_INITIALIZER;
    venc_cxt->cond = PTHREAD_COND_INITIALIZER;
    //out file
    venc_cxt->out_file = createOutFile(OUT_PATH);
    if(!venc_cxt->out_file)    return -1;
    
    //encoder
    venc_cxt->pEncoder = VideoEncCreate(VENC_CODEC_H264);
    //* h264 param
    VencH264Param h264Param;
    int value;
	h264Param.bEntropyCodingCABAC = 1;
	h264Param.nBitrate = 4*1024*1024; /* bps */
	h264Param.nFramerate = 30; /* fps */
	h264Param.nCodingMode = VENC_FRAME_CODING;
	
	h264Param.nMaxKeyInterval = 30;
	h264Param.sProfileLevel.nProfile = VENC_H264ProfileMain;
	h264Param.sProfileLevel.nLevel = VENC_H264Level31;
	h264Param.sQPRange.nMinqp = 10;
	h264Param.sQPRange.nMaxqp = 40;
    VideoEncSetParameter(venc_cxt->pEncoder, VENC_IndexParamH264Param, &h264Param);
    value = 0;
	VideoEncSetParameter(venc_cxt->pEncoder, VENC_IndexParamIfilter, &value);
	value = 0; //degree
	VideoEncSetParameter(venc_cxt->pEncoder, VENC_IndexParamRotation, &value);
    //fix qp mode
    VencH264FixQP fixQP;
	fixQP.bEnable = 1;
	fixQP.nIQp = 20;
	fixQP.nPQp = 30;
    VideoEncSetParameter(venc_cxt->pEncoder, VENC_IndexParamH264FixQP, &fixQP);

    //baseConfig
    memset(&venc_cxt->baseConfig, 0, sizeof(VencBaseConfig));
    venc_cxt->baseConfig.nInputWidth= 640;
	venc_cxt->baseConfig.nInputHeight = 480;
	venc_cxt->baseConfig.nStride = 640;
	
	venc_cxt->baseConfig.nDstWidth = 640;
	venc_cxt->baseConfig.nDstHeight = 480;
    venc_cxt->baseConfig.eInputFormat = VENC_PIXEL_YUV420P;
    VideoEncInit(venc_cxt->pEncoder, &venc_cxt->baseConfig);
    //addFileHeader
    unsigned int head_num = 0;
    VencHeaderData sps_pps_data;
	VideoEncGetParameter(venc_cxt->pEncoder, VENC_IndexParamH264SPSPPS, &sps_pps_data);
	fwrite(sps_pps_data.pBuffer, 1, sps_pps_data.nLength, venc_cxt->out_file);
	logd("sps_pps_data.nLength: %d", sps_pps_data.nLength);
	for(head_num=0; head_num<sps_pps_data.nLength; head_num++)
	    logd("the sps_pps :%02x\n", *(sps_pps_data.pBuffer+head_num));
    
    //alloc buf
    VencAllocateBufferParam bufferParam;
    memset(&bufferParam, 0 ,sizeof(VencAllocateBufferParam));
    bufferParam.nSizeY = venc_cxt->baseConfig.nInputWidth * venc_cxt->baseConfig.nInputHeight;
	bufferParam.nSizeC = venc_cxt->baseConfig.nInputWidth * venc_cxt->baseConfig.nInputHeight/2;
	bufferParam.nBufferNum = 4;
    AllocInputBuffer(venc_cxt->pEncoder, &bufferParam);
    
    //camera
    camera = new Camera(DEV_PATH);
    
    list<CameraFmt> *fmtlist = camera->getSupportfmt();
    cout << "count " << fmtlist->size() << endl;
#ifdef DEBUG
    for(list<CameraFmt>::iterator it = fmtlist->begin(); it != fmtlist->end(); it++)
    {
        printf("%s w:%d h: %d\n", it->description, it->width, it->height);
    }
#endif //DEBUG

    if(!camera->init(*fmtlist->begin()))
        goto err;
    delete fmtlist;

    ret = pthread_create(&venc_cxt->tid, NULL, encode_thread, venc_cxt);
    if(ret < 0)
    {
        logp("thread create err\n");
        goto err;
    }

    //开始
    camera->setCallBack(dealData, venc_cxt);
    camera->start();
    camera->loop();

    logd("camera close.\n");
    pthread_join(venc_cxt->tid, NULL);
    VideoEncUnInit(venc_cxt->pEncoder);
    VideoEncDestroy(venc_cxt->pEncoder);
    fclose(venc_cxt->out_file);
    delete camera;
    delete venc_cxt;
    return 0;
err:
    VideoEncDestroy(venc_cxt->pEncoder);
    fclose(venc_cxt->out_file);
    delete camera;
    delete venc_cxt;
    return -1;
}

FILE *createOutFile(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if(fp == NULL)
    {
        logp(path);
        return NULL;
    }
    return fp;
}

int yuv_422to420p(const uint8_t *src, uint8_t *dest_y, uint8_t *dest_c, uint32_t width, uint32_t height)
{
    //420p中yuv的分布
    uint8_t *y = dest_y;
    uint8_t *u = dest_c;
    uint8_t *v = dest_c + width * height / 4;

    uint32_t i, j;
    unsigned int base_h;
    unsigned int is_u = 1;
    unsigned int y_index = 0, u_index = 0, v_index = 0;
 
    unsigned long yuv422_length = 2 * width * height;

    for (i = 0; i < yuv422_length; i += 2)
    {
        *(y + y_index) = *(src + i);
        y_index++;
    }
 
    for (i = 0; i < height; i += 2)
    {
        base_h = i*width*2;
        for (j = base_h + 1; j < base_h + width * 2; j += 2)
        {
            if(is_u)
            {
                *(u + u_index) = *(src + j);
                u_index++;
                is_u = 0;
            }
            else
            {
                *(v + v_index) = *(src + j);
                v_index++;
                is_u = 1;
            }
        }
    }
    return 0;
}