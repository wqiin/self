#include "CGetTask.h"
//c头文件

//C++库头文件
#include <unordered_map> //std::unordered_map
#include <future> //std::async, std::future
#include <chrono> //std::chrono
#include <iomanip> //std::put_time, std::get_time
#include <sstream> //std::ostringstream
#include <utility> //std::move
#include <stdexcept> //std::runtime_error
#include <cctype> //std::isdigit

//项目库头文件
#include "curl/curl.h"

//项目头文件

//重命名std::unordered_map
template <typename KeyType, typename ValueType>
using hash_map = std::unordered_map<KeyType, ValueType>;

//检测类型枚举，映射为检测类型字符串
static const hash_map<DetectType, std::string> g_mpEn2DetectType{
    std::pair<DetectType, std::string>{ DetectType::EN_HOLLOW_AXLE, std::string("FLAW_DETECTION_AXLE") },//空心车轴探伤
    std::pair<DetectType, std::string>{ DetectType::EN_LU, std::string("FLAW_DETECTION_LU") }//轮辋轮辐探伤
};

//key为任务ID，value为该任务的JSON对象
typedef hash_map<std::string, Json::Value> MapTasks;

//从左到右参数依次为，Json::value为班组信息，std::string探伤工姓名，std::vector<std::string>>该探伤工被分配的任务ID
typedef std::vector<std::pair<Json::Value, hash_map<std::string, std::vector<std::string>>>> VecWorkers;


//获取任务信息 - std::string为任务ID，Json::Value为该任务的JSON对象,enDetectType为检测类型
static MapTasks getTasksInfo(const Json::Value & jsData, const DetectType enDetectType){
    MapTasks mpRet;//函数的返回值

    int nCode = jsData["code"].asInt();
    if(1 != nCode)//1为成功，其他值为失败
        return mpRet;

    const Json::Value & jsArrData = jsData["data"];
    for(const auto & item : jsArrData){
        const Json::Value & jsTaskTree = item["taskTree"];//提取taskTree字段

        //筛选检测类型为enDetectType的任务
        const std::string && strDetectType = jsTaskTree["type"].asString();
        if (strDetectType != g_mpEn2DetectType.at(enDetectType))
            continue;

        //解析TaskTree中的children字段
        auto getChild = [&mpRet](auto && self, const Json::Value & jsArrChild)->void{
            if(jsArrChild.empty())
                return ;

            for(const auto & item : jsArrChild){
                const std::string && strTaskId = item["id"].asString();//任务ID
                mpRet.emplace(strTaskId, item);

                const Json::Value & jsChilds = item["children"];//对children字段，递归调用该方法
                if(!jsChilds.empty()){
                    self(self, jsChilds);
                }
            }
        };

        const Json::Value & jsArrTasks = jsTaskTree["children"];
        getChild(getChild, jsArrTasks);//递归提取children字段

        const std::string && strTaskId = jsTaskTree["id"].asString();//该任务的任务ID
        mpRet.emplace(strTaskId, jsTaskTree);
    }

    return mpRet;
}

//从左到右参数依次为，Json::value为班组信息，std::string探伤工姓名，std::vector<std::string>>该探伤工被分配的任务ID
//获取探伤工被分配的任务信息
static VecWorkers getWorkersInfo(const Json::Value & jsData){
    VecWorkers vecRet;//函数返回值

    int nCode = jsData["code"].asInt();
    if(1 != nCode)//1为成功，其他值为失败
        return vecRet;

    const Json::Value & jsArrData = jsData["data"];
    for(const auto & item : jsArrData){
        const Json::Value & jsTeamInfo = item["teamAllot"];//任务班组信息
        const Json::Value & jsArrWorkers = item["workerAllots"];//探伤工派工信息
        hash_map<std::string, std::vector<std::string>> mpWorker2Task;

        for(const auto & work : jsArrWorkers){
            const std::string & strTaskId = work["taskId"].asString();//任务ID
            const std::string & strWorkName = work["workerName"].asString();//探伤工名称

            if(mpWorker2Task.find(strWorkName) != mpWorker2Task.end()){//已经存在改探伤工，直接插入
                mpWorker2Task[strWorkName].push_back(strTaskId);
            }else{
                mpWorker2Task.emplace(strWorkName, std::vector<std::string>{strTaskId});//原地构造
            }
        }

        vecRet.emplace_back(jsTeamInfo, std::move(mpWorker2Task));
    }

    return vecRet;
}


//判断传入的检测类型枚举值是否有效
auto isTypeValid = [](DetectType enType)->bool {
    //return g_mpEn2DetectType.find(enType) != g_mpEn2DetectType.end();
    return g_mpEn2DetectType.count(enType);//这种实现更简洁
};

//递归遍历jsChild中的children字段，并将子任务存入vecRet，
auto getChildren = [](auto && self, const Json::Value & jsChild, std::vector<StTaskInfo> & vecRet)->void {
    if (jsChild.empty())
        return;

    for (const auto & item : jsChild) {
        const std::string && strTaskMode = item["mode"].asString();//任务模式
        if (std::string("PART") != strTaskMode) {//非PART层级任务，递归遍历
            const Json::Value & jsChildrenInner = item["children"];
            self(self, jsChildrenInner, vecRet);//递归遍历
        }else {//已经是PART层级任务
            vecRet.emplace_back(StTaskInfo{});//原地构建
            CGetTask::convertTask(item, vecRet.back());//任务转换为StTaskInfo
        }
    }
};

//从jsData中，解析名为strUserName且类型为DetectType任务，并返回
auto parseTasks = [](const Json::Value & jsData, const std::string & strUserName, const DetectType enDetectType)->std::vector<StTaskInfo>{
    //异步启用现场解析任务和探伤工信息
    std::future<MapTasks> && ftTasks = std::async(std::launch::async, getTasksInfo, jsData, enDetectType);
    std::future<VecWorkers> && ftWorkers = std::async(std::launch::async, getWorkersInfo, jsData);

    //阻塞等待解析结束
    MapTasks && mpTasks = ftTasks.get();//任务信息
    VecWorkers && vecWorkers = ftWorkers.get();//探伤工被分配任务信息

    std::vector<StTaskInfo> vecRet;//函数返回值
    for (const auto & item : vecWorkers) {
        for (const auto & work : item.second) {
            if (strUserName == work.first) {//相同姓名
                for (const auto & taskId : work.second) {
                    if (mpTasks.find(taskId) != mpTasks.end()) {//任务列表存在该任务ID时，才添加至返回值中
                        //需要判断mpTasks[taskId]的mode，即判断任务层级
                        const Json::Value & jsTask = mpTasks[taskId];
                        const std::string & strTaskMode = jsTask["mode"].asString();//任务mode

                        if (std::string("PART") != strTaskMode) {//非PART层级任务，递归遍历
                            const Json::Value & jsChildren = jsTask["children"];
                            getChildren(getChildren, jsChildren, vecRet);//递归遍历其子任务
                        }else {//已经是PART层级任务
                            vecRet.emplace_back(StTaskInfo{});//原地构建
                            CGetTask::convertTask(mpTasks[taskId], vecRet.back());//任务转换为StTaskInfo
                        }
                    }
                }
            }
        }
    }

    return vecRet;
};

//返回引用后，可以链式调用
CGetTask & CGetTask::setUserName(const std::string & strUserName){
    this->m_strUserName = strUserName;
    return *this;
}

//设置URL地址
CGetTask & CGetTask::setURL(const std::string & strURL){
    this->m_strURL = strURL;
    return *this;
}

//设置检测类型
CGetTask & CGetTask::setDetectType(const DetectType enType){
    this->m_enDetectType = enType;
     return *this;
}

//设置任务过滤的时间段
CGetTask & CGetTask::setTimeSpan(const TimeSpan & tmSpan) {
    this->m_tmSpan = tmSpan;
    return *this;
}

//获取任务过滤的时间段
TimeSpan CGetTask::getTimeSpan() const {
    return this->m_tmSpan;
}

//获取检测类型
DetectType CGetTask::getDetectType() const {
    return this->m_enDetectType;
}

//获取URL地址
std::string CGetTask::getURL() const {
    return this->m_strURL;
}

//获取用户名称
std::string CGetTask::getUserName() const{
    return this->m_strUserName;
}

//构造函数
CGetTask::CGetTask(const std::string & strURL, const std::string & strUserName, const DetectType enDetectType, const TimeSpan & tmSpan){
    this->m_strUserName = strUserName;
    this->m_strURL = strURL;
    this->m_enDetectType = enDetectType;//默认使用空心轴检测类型
    this->m_tmSpan = tmSpan;
}

//构造函数
CGetTask::CGetTask(){
    this->m_strUserName = "";
    this->m_strURL = "";
    this->m_enDetectType = DetectType::EN_HOLLOW_AXLE;//默认使用空心轴检测类型
    this->m_tmSpan = NOWDAY;//默认使用当前日期
}

//返回姓名为m_strUserName的任务信息，以json对象返回，传入参数:bool指针，用以标识调用是否成功
std::vector<StTaskInfo> CGetTask::getTask(bool* pBool) const{
    if(0){
 _error_handle://处理函数错误
        if(pBool)
            *pBool = false;
        return std::vector<StTaskInfo>();
    }

    //检测类型有效性校验
    if (!isTypeValid(this->m_enDetectType))
        goto _error_handle;

    //从SLPI中拉取json信息
    std::string strJson;//存放从SLPI拉取过来的数据
    if(this->m_strUserName.empty() || this->m_strURL.empty() ||
        !CGetTask::getDataFromRemote(this->m_strURL, this->m_tmSpan, strJson)){
        goto _error_handle;
    }

    //json解析成对象
    Json::Value jsData;
    if(!Json::Reader().parse(strJson, jsData)){
        goto _error_handle;
    }

    //从jsData中解析任务，并返回
    return parseTasks(jsData, this->m_strUserName, this->m_enDetectType);
}

//使用类的成员变量从SLPI中拉取数据，并返回
std::optional<std::vector<StTaskInfo>> CGetTask::getTask() const {
    return CGetTask::getTask(this->m_strUserName, this->m_strURL, this->m_tmSpan, this->m_enDetectType);
}


//从给定的strURL中，返回timeSpan.first开始，timeSpan.second结束，且探伤工名为strUserName和检测类型为strType的任务信息. 注:日期字符串格式为std::string("yyyy-mm-dd"),例如std::string("2024-11-03")，如给定的时间段无效，则使用当前日期
std::optional<std::vector<StTaskInfo>> CGetTask::getTask(const std::string & strUserName, const std::string & strURL, const TimeSpan & timeSpan, const DetectType enDetectType)
{
    //检测类型参数有效性判断
    if (!isTypeValid(enDetectType))
        return std::nullopt;

    //从SLPI中拉取json数据，并解析
    std::string strJson;
    if (strUserName.empty() || !CGetTask::getDataFromRemote(strURL, timeSpan, strJson)) {
        return std::nullopt;
    }

    //json解析
    Json::Value jsData;
    if (!Json::Reader().parse(strJson, jsData)) {
        return std::nullopt;
    }

    //从jsData中解析任务，并返回
    return parseTasks(jsData, strUserName, enDetectType);
}


//CURL库回调函数
std::uint32_t CGetTask::writeCallback(void * contents, std::uint32_t size, std::uint32_t nmemb, void * pUserData){
    ((std::string*)pUserData)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

//获取当前日期，并以yyyy-mm-dd格式的字符串返回
std::string CGetTask::getNowDate(){
    //获取当前的时间点
    auto && now  = std::chrono::system_clock::now();

    //将时间点转换为std::time_t类型，为自1970到现在的秒数
    std::time_t nowTime_t = std::chrono::system_clock::to_time_t(now);

    //将std::time_t转为std::tm
    std::tm nowTm = *std::localtime(&nowTime_t);

    //使用字符串格式化字符串
    std::ostringstream oss;
    oss << std::put_time(&nowTm, "%Y-%m-%d");

    return oss.str();//返回当前日期
}

//在给定的URL地址，从SLPI拉取timeSpan.first开始，timeSpan.second结束的原始JSON数据，存放至strJsData;如果timeSpan错误、无效，则使用当前日期
bool CGetTask::getDataFromRemote(const std::string & strURL, const TimeSpan & timeSpan, std::string & strJsData){
    //校验传入的timeSpan时间段是否有效
    auto checkTimeSpan = [&timeSpan]()->bool{
        if(timeSpan.first.empty() || timeSpan.second.empty())
            return false;

        //将字符串时间，转换为std::time_t时间
        auto getTime_t = [](const std::string & strDate)->std::optional<std::time_t>{
            std::tm tm;
            std::istringstream ss(strDate);

            // 解析日期字符串为 tm 结构
            ss >> std::get_time(&tm, "%Y-%m-%d");
            if (ss.fail()) {
                return std::nullopt;
            }

            // 将 tm 转换为 time_t
            return std::mktime(&tm);
        };

        auto && opStartTime = getTime_t(timeSpan.first);//开始日期
        auto && opEndTime = getTime_t(timeSpan.second);//结束日期
        if(!opStartTime.has_value() || !opEndTime.has_value())//返回值无效
            return false;

        if(*opStartTime > *opEndTime)//其实时间大于结束时间
            return false;

        return true;
    };


    TimeSpan timeSpanLocal = timeSpan;//日期段
    if(strURL.empty())
        return false;

    if(!checkTimeSpan()){//如果传入的日期段无效，则使用当前日期
        timeSpanLocal = NOWDAY;
    }

    //需要获取当前时间，组装获取任务的起始时间 &startDate=2014-10-14&endDate=2024-10-14
    const std::string && strURLSuffix = std::string("&startDate=") + timeSpanLocal.first + std::string("&endDate=") + timeSpanLocal.second;//URL后缀，仅获取当天的任务
    const std::string && strURLTemp = strURL + strURLSuffix;//拼装URL，加上过滤日期

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL * curl = curl_easy_init();
    bool bRet = true;//函数返回值
    if(curl){
        std::string strRetJsonData;//存放从SLPI拉取的JSON数据
        curl_easy_setopt(curl, CURLOPT_URL, strURLTemp.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CGetTask::writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &strRetJsonData);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000);//设置超时时间3秒
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000);//连接超时
        CURLcode res = curl_easy_perform(curl);

        if(res != CURLE_OK){
            bRet = false;
        }else {//成功拉取数据后，进行转码
            strJsData = CGetTask::utf8toAnsi(strRetJsonData);
        }

        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return bRet;
}

//utf8字符串转Ansi字符串
std::string CGetTask::utf8toAnsi(const std::string & strUTF8)
{
#if defined(_WIN32) || defined(_WIND64)
    if (strUTF8.empty())
        return std::string("");

    int nLength = MultiByteToWideChar(CP_UTF8, 0, strUTF8.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> vecUnicode(nLength, 0);
    MultiByteToWideChar(CP_UTF8, 0, strUTF8.c_str(), -1, vecUnicode.data(), nLength);

    nLength = WideCharToMultiByte(CP_ACP, 0, vecUnicode.data(), -1, NULL, 0, NULL, NULL);
    std::vector<char> vecAnsi(nLength, 0);
    WideCharToMultiByte(CP_ACP, 0, vecUnicode.data(), -1, vecAnsi.data(), nLength, NULL, NULL);
    return std::string(vecAnsi.data());
#elif defined(__APPLE__) || defined(__MACH__) || defined(__linux__) || defined(__unix__)

    // 打开 iconv 转换描述符，将 UTF-8 转为 ISO-8859-1（ANSI 编码）
    iconv_t cd = iconv_open("ISO-8859-1//IGNORE", "UTF-8");
    if (cd == (iconv_t)-1) {
        throw std::runtime_error("iconv_open failed");
    }

    // 设置输入和输出指针
    const char* pSrc = strUTF8.c_str();
    size_t nUTF8Size = strUTF8.size();

    // 为输出缓冲区分配足够的空间
    std::vector<char> vecOutBuf(nUTF8Size * 2);  // 足够的空间用于存储转换后的字符串
    char* pOut = vecOutBuf.data();
    size_t nOutSize = vecOutBuf.size();

    // 执行编码转换
    size_t nRetSize = iconv(cd, const_cast<char**>(&pSrc), &nUTF8Size, &pOut, &nOutSize);
    if (nRetSize == (size_t) -1) {
        iconv_close(cd);
        throw std::runtime_error("iconv conversion failed");
    }

    // 关闭转换描述符
    iconv_close(cd);

    // 返回转换后的 ANSI 字符串
    return std::string(vecOutBuf.data(), vecOutBuf.size() - nOutSize);
#else
#error “Unsupported platform environment!!!”
#endif
    return std::string();
}

//将jsTask任务信息转换为stTask任务信息
void CGetTask::convertTask(const Json::Value & jsTask, StTaskInfo & stTask) {
    //解析任务信息的name字段，提取车厢号，轴号
    auto parseData = [&stTask](std::string strName)->void {
        auto && strTrainNo = strName.substr(0, 2);
        stTask.nCarriageId = std::stoi(strTrainNo); //车厢号转换为数字

        //获取括号内的字符串并转换为数字
        size_t startPos = strName.find('(');
        size_t endPos = strName.find(')');
        if (startPos != std::string::npos && endPos != std::string::npos) {
            const std::string && strUnknownContent = strName.substr(startPos + 1, endPos - startPos - 1);
            stTask.nUnknown = std::stoi(strUnknownContent); //括号内的无意义字段，转换为数字
            strName.erase(startPos, endPos - startPos + 1);// 删除括号和内容
        }

        //提取轴号，删除前两个字节，遍历剩下的字符串并提取数字
        strName.erase(0, 2); // 删除前两个字符
        for (char c : strName) {
            if (std::isdigit(static_cast<unsigned char>(c))) { // 判断是否为数字
                stTask.nAxleId = stTask.nAxleId * 10 + (c - '0'); // 转换为数字
            }
        }
    };

    stTask.strTrainSetName = jsTask["trainsetName"].asString();//车组名称
    const std::string && strName = jsTask["name"].asString();//任务的name字段
    parseData(strName);//解析车号和轴号
}

//将jsTask任务信息转换为stTask任务信息
void CGetTask::convertTask(const std::vector<Json::Value> & vecJsTask, std::vector<StTaskInfo> & vecStTask) {
    for (const auto & item : vecJsTask) {
        vecStTask.emplace_back(StTaskInfo{});//原地构造
        convertTask(item, vecStTask.back());//任务转换
    }
}
