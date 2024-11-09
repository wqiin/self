
#ifndef _CGETTASK_H_
#define _CGETTASK_H_

#include "json/json.h"

#include <string>
#include <vector>
#include <optional>
#include <cstdint>



//时间段, first为开始时间，second为结束时间
typedef std::pair<std::string, std::string> TimeSpan;

//当前日期的TimeSpan对象
#define NOWDAY TimeSpan{CGetTask::getNowDay(), CGetTask::getNowDay()}


//任务检测类型
enum EN_detectType:char {
	en_Hollow_Axle = 0,	//空心轴探伤
	en_LU,				//轮辋轮辐探伤

	en_invalid_detect_type,	//无效检测类型
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
public:
    //CURL回调写方法
    static std::uint32_t writeCallback(void * contents, std::uint32_t size, std::uint32_t nmemb, void * pUserData);

    //获取当前日期，并以yyyy-mm-dd格式的字符串返回
    static std::string getNowDay();

    //在给定的URL地址，从SLPI拉取timeSpan.first开始，timeSpan.second结束的原始JSON数据，存放至strJsData；如果timeSpan错误、无效，则使用当前日期
    static bool getDataFromRemote(const std::string & strURL, const TimeSpan & timeSpan, std::string & strJsData);

	//utf8字符串转Ansi字符串
	static std::string utf8toAnsi(const std::string & strUTF8);

	//将jsTask任务信息转换为stTask任务信息
	static void convertTask(const Json::Value & jsTask, StTaskInfo & stTask);
	static void convertTask(const std::vector<Json::Value> & vecJsTask, std::vector<StTaskInfo> & vecStTask);

public:
	CGetTask(const std::string & strURL, const std::string & strUserName, const EN_detectType enDetectType = en_Hollow_Axle, const TimeSpan & tmSpan = NOWDAY);
    CGetTask();

    void setUserName(const std::string & strUserName);//设置探伤工名称
    void setURL(const std::string & strURL);//设置URL地址
	void setDetectType(const EN_detectType enType);//设置检测类型
	void setTimeSpan(const TimeSpan & tmSpan);//设置任务过滤的时间段

	EN_detectType getDetectType();//获取检测类型
	std::string getURL();//获取URL地址
	std::string getUserName();//获取探伤工名称
	TimeSpan getTimeSpan();//获取任务过滤的时间段

    //获取姓名为m_strUserName任务信息，以json对象形式存放在vector中
    std::vector<StTaskInfo> getTask(bool * pBool);//pBool用以返回调用是否成功
    std::optional<std::vector<StTaskInfo>> getTask();//默认使用m_strUserName筛选，从m_strURL中获取
    std::optional<std::vector<StTaskInfo>> getTask(const std::string & strUserName);//返回名为strUserName的任务信息，从m_strURL中获取
    std::optional<std::vector<StTaskInfo>> getTask(const std::string & strUserName, const std::string & strURL);//从给定的strURL中，返回名为strUserName的任务信息

    //从给定的strURL中，返回timeSpan.first开始，timeSpan.second结束，且名为strUserName的任务信息. 注:日期字符串格式为std::string("yyyy-mm-dd"),例如std::string("2024-11-03")，如给定的时间段无效，则使用当前日期
    std::optional<std::vector<StTaskInfo>> getTask(const std::string & strUserName, const std::string & strURL, const TimeSpan & timeSpan);

	//从给定的strURL中，返回timeSpan.first开始，timeSpan.second结束，且探伤工名为strUserName和检测类型为strType的任务信息. 注:日期字符串格式为std::string("yyyy-mm-dd"),例如std::string("2024-11-03")，如给定的时间段无效，则使用当前日期
	static std::optional<std::vector<StTaskInfo>> getTask(const std::string & strUserName, const std::string & strURL, const TimeSpan & timeSpan, const EN_detectType enDetectType);

private://用以获取或过滤的条件
    std::string m_strUserName;//用户姓名
    std::string m_strURL;//SLPI资源URL地址
	EN_detectType m_enDetectType;//任务检测类型
	TimeSpan m_tmSpan;//任务时间段
};


#endif
