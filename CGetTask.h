
#ifndef _CGETTASK_H_
#define _CGETTASK_H_

#include "json/json.h"

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#if defined(__APPLE__) || defined(__MACH__) || defined(__linux__) || defined(__unix__)
#include <iconv.h>
#endif

/************************************************
 *  使用示例，可以选择使用对象，也可以选择使用静态方法  *
 ************************************************
    //使用构造函数的传入参数，默认拉取
    {
        CGetTask test(std::string("URL"), std::string("UserName"));
        auto && tasksInfo = test.getTask();
        if(tasksInfo.has_value()){
            for(const auto & item : *tasksInfo){
                std::cout << item.strTrainSetName << std::endl;
            }
        }
    }

     //链式调用
    {
        CGetTask test;
        auto && tasks = test.setURL(std::string("URL"))
                            .setUserName(std::string("UserName"))
                            .setTimeSpan(NOWDAY)
                            .setDetectType(EN_LU)
                            .getTask();

        if(tasks.has_value()){
            for(const auto & item : *tasks){
                std::cout << "trainSetName:" << item.strTrainSetName << std::endl;
            }
        }
    }

    //使用静态成员拉取
    {
        auto && tasksInfo = CGetTask::getTask(std::string("URL"), std::string("UserName"), NOWDAY, EN_LU);
        if(tasksInfo.has_value()){
            for(const auto & item : *tasksInfo){
                std::cout << "trainSetName:" << item.strTrainSetName << std::endl;
            }
        }
    }
*/


//禁用拷贝和赋值运算符
#define DISALLOW_COPY_AND_ASSIGN(typeName)\
private:\
    typeName(const typeName &); \
    void operator=(const typeName &);\

//当前日期的TimeSpan对象
#define NOWDAY TimeSpan{CGetTask::getNowDate(), CGetTask::getNowDate()}

//时间段类型, first为开始时间，second为结束时间
typedef std::pair<std::string, std::string> TimeSpan;

//任务检测类型
enum DetectType:char {
    EN_HOLLOW_AXLE = 0,	//空心轴探伤
    EN_LU,				//轮辋轮辐探伤

    EN_INVALID_DETECT_TYPE,	//无效检测类型
};

//探伤任务详细信息
typedef struct ST_taskInfo {
    std::uint32_t nCarriageId = 0x00;//车厢号
    std::uint32_t nAxleId = 0x00;//轴号
    std::uint32_t nUnknown = 0x00;//任务中name字段中的括号的内容；意义未知

    std::string strTrainSetName = "";//车组名称
}StTaskInfo;


//added by zhangys 新增CGetTask，用以从SPLI拉取任务信息，并按照给定的条件过滤
class CGetTask{
    DISALLOW_COPY_AND_ASSIGN(CGetTask);

public:
    CGetTask(const std::string & strURL, const std::string & strUserName, const DetectType enDetectType = EN_HOLLOW_AXLE, const TimeSpan & tmSpan = NOWDAY);
    CGetTask();

public:
    //默认使用类的成员变量作为过滤条件，从SLPI中拉取信息
    std::vector<StTaskInfo> getTask(bool * pBool) const;//pBool用以返回调用是否成功
    std::optional<std::vector<StTaskInfo>> getTask() const;//使用类的成员变量，拉取信息

public://返回引用，可以实现链式调用
    CGetTask & setUserName(const std::string & strUserName);//设置探伤工名称
    CGetTask & setURL(const std::string & strURL);//设置URL地址
    CGetTask & setDetectType(const DetectType enType);//设置检测类型
    CGetTask & setTimeSpan(const TimeSpan & tmSpan);//设置任务过滤的时间段

    DetectType getDetectType() const;//获取检测类型
    std::string getURL() const;//获取URL地址
    std::string getUserName() const;//获取探伤工名称
    TimeSpan getTimeSpan() const;//获取任务过滤的时间段

private://用以获取或过滤的条件
    std::string m_strUserName;//用户姓名
    std::string m_strURL;//SLPI资源URL地址
    DetectType m_enDetectType;//任务检测类型
    TimeSpan m_tmSpan;//任务时间段

public:
    //CURL回调写方法
    static std::uint32_t writeCallback(void * contents, std::uint32_t size, std::uint32_t nmemb, void * pUserData);

    //获取当前日期，并以yyyy-mm-dd格式的字符串返回
    static std::string getNowDate();

    //在给定的URL地址，从SLPI拉取timeSpan.first开始，timeSpan.second结束的原始JSON数据，存放至strJsData；如果timeSpan错误、无效，则使用当前日期
    static bool getDataFromRemote(const std::string & strURL, const TimeSpan & timeSpan, std::string & strJsData);

    //utf8字符串转Ansi字符串
    static std::string utf8toAnsi(const std::string & strUTF8);

    //将jsTask任务信息转换为stTask任务信息
    static void convertTask(const Json::Value & jsTask, StTaskInfo & stTask);
    static void convertTask(const std::vector<Json::Value> & vecJsTask, std::vector<StTaskInfo> & vecStTask);

    //从给定的strURL中，返回timeSpan.first开始，timeSpan.second结束，且探伤工名为strUserName和检测类型为strType的任务信息. 注:日期字符串格式为std::string("yyyy-mm-dd"),例如std::string("2024-11-03")，如给定的时间段无效，则使用当前日期
    static std::optional<std::vector<StTaskInfo>> getTask(const std::string & strUserName, const std::string & strURL, const TimeSpan & timeSpan, const DetectType enDetectType);
};

#endif
