/**
 * camera.h
 */
#ifndef _VIDEO_H
#define _VIDEO_H

#include <stdint.h>
#include <list>
#include <linux/videodev2.h>

using namespace std;

struct CameraFmt {
    char     description[32];
    uint32_t pixelformat; //v4l2格式
    uint32_t width;
    uint32_t height;
};

struct MemInfo {
    void        *addr;
    uint32_t    len;
};

typedef void (*dealData_cb) (struct v4l2_buffer *runbuf, struct MemInfo *memMap, void *tag);

class Camera
{
public:
    explicit Camera(const char *dev, uint32_t memCount = 3);
    ~Camera();
    bool init(CameraFmt & camerafmt);
    list<CameraFmt> *getSupportfmt(); //使用完记得delete list
    bool start();
    bool stop();
    void loop();
    bool isLoop() { return isloop; }
    void disloop() { isloop = false; }
    bool isStart() { return isstart; }
    void setCallBack(dealData_cb cb, void *tag) { callback = cb; cb_tag = tag; }
    bool returnFrame(uint32_t i);
private:
    long getFrame();
private:
    bool isstart;
    bool isloop;
    int         devfd; // 打开后的文件描述符
    CameraFmt   fmt; // 摄像头采集的格式
    
    uint32_t    memCount; // 缓冲区个数
    struct MemInfo *memMaps; // 缓冲区数组

    struct v4l2_buffer runbuf; //getYuyvData用到的

    dealData_cb callback;
    void        *cb_tag;
};


#endif //_VIDEO_H