/*
   Mathieu Stefani, 07 février 2016

   Example of a REST endpoint with routing
*/

#include <algorithm>

#include <pistache/http.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/document.h>
#include <iostream>
#include <string>
#include <vector>

extern "C"
{
#include "quota.c"
}

using namespace std;
using namespace Pistache;

void printCookies(const Http::Request &req)
{
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto &c : cookies)
    {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

struct ProjectQuota
{
    string blockUsed;
    string blockSoftLimit;
    string blockHardLimit;
    string inodeUsed;
    string inodeSoftLimit;
    string inodeHardLimit;
};

struct ProjectQuotaResp
{
    int code;
    string msg;
    struct ProjectQuota data;
};

namespace Generic
{

    void handleReady(const Rest::Request &, Http::ResponseWriter response)
    {
        string queryArr[] = {"quota", "-p", "2", "/mnt/lustre"};
        int length = sizeof(queryArr) / sizeof(queryArr[0]);
        char **listOfNames;
        listOfNames = new char *[length];
        for (size_t i = 0; i < length; i++)
        {
            // listOfNames[i] = queryArr[i].c_str();
            // strcpy(listOfNames[i],queryArr[i].c_str());
            // listOfNames[i] = "hello";
            listOfNames[i] = &queryArr[i][0];
        }

        struct project_quota_resp xxx;
        // int arena_lfs_quota(int argc, char **argv, project_quota_resp *pqr)
        int r = arena_lfs_quota(4, listOfNames, &xxx);

        printf("xxblock_used: %s \n", xxx.block_used);
        printf("xxblock_soft_limit: %s \n", xxx.block_soft_limit);
        printf("xxblock_hard_limit: %s \n", xxx.block_hard_limit);
        printf("xxinode_used: %s \n", xxx.inode_used);
        printf("xxinode_soft_limit: %s \n", xxx.inode_soft_limit);
        printf("xxinode_hard_limit: %s \n", xxx.inode_hard_limit);

        // https://www.cnblogs.com/rainsoul/p/10408565.html
        rapidjson::StringBuffer strBuf;
        rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
        writer.StartObject();
        writer.Key("code");
        writer.Int(r);
        if (r == 0)
        {
            writer.Key("msg");
            writer.String("success");

            writer.Key("data");
            writer.StartObject();
            writer.Key("block_used");
            writer.String(xxx.block_used);
            writer.Key("block_soft_limit");
            writer.String(xxx.block_soft_limit);
            writer.Key("block_hard_limit");
            writer.String(xxx.block_hard_limit);
            writer.Key("inode_used");
            writer.String(xxx.inode_used);
            writer.Key("inode_soft_limit");
            writer.String(xxx.inode_soft_limit);
            writer.Key("inode_hard_limit");
            writer.String(xxx.inode_hard_limit);
            writer.EndObject();
        }
        else
        {
            writer.Key("msg");
            writer.String("fail");
        }

        writer.EndObject();

        string jsonStr = strBuf.GetString();

        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(Http::Code::Ok, jsonStr);
    }

} // namespace Generic

class StatsEndpoint
{
public:
    explicit StatsEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    {
    }

    void init(size_t thr = 2)
    {
        auto opts = Http::Endpoint::options()
                        .threads(static_cast<int>(thr));
        httpEndpoint->init(opts);
        setupRoutes();
    }

    void start()
    {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serve();
    }

private:
    void setupRoutes()
    {
        using namespace Rest;

        Routes::Post(router, "/record/:name/:value?", Routes::bind(&StatsEndpoint::doRecordMetric, this));
        Routes::Get(router, "/value/:name", Routes::bind(&StatsEndpoint::doGetMetric, this));
        Routes::Post(router, "/lfs/quota", Routes::bind(&StatsEndpoint::doGetQuota, this));
        Routes::Post(router, "/lfs/setquota", Routes::bind(&StatsEndpoint::doSetQuota, this));
        Routes::Post(router, "/lfs/setproject", Routes::bind(&StatsEndpoint::doSetProject, this));
        Routes::Post(router, "/lfs/clearproject", Routes::bind(&StatsEndpoint::doClearProject, this));
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&StatsEndpoint::doAuth, this));
    }

    void doGetQuota(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto reqBody = request.body();
        rapidjson::Document doc;
        if (!doc.Parse(reqBody.data()).HasParseError())
        {
            if (doc.HasMember("project") && doc["project"].IsUint() && doc.HasMember("mount_point") && doc["mount_point"].IsString())
            {
                cout << "project = " << doc["project"].GetUint() << endl;
                cout << "mount_point = " << doc["mount_point"].GetString() << endl;

                vector<string> cmdArr;
                cmdArr.push_back("quota");
                cmdArr.push_back("-pool");
                cmdArr.push_back(to_string(doc["project"].GetUint()));
                cmdArr.push_back(doc["mount_point"].GetString());

                int cmdArrLen = cmdArr.size();

                char *listOfCmds[4] = {0};

                for (int i = 0; i < cmdArrLen; i++)
                {
                    listOfCmds[i] = &cmdArr[i][0];
                }

                struct project_quota_resp pqr;
                // static int arena_lfs_quota(int argc, char **argv, struct project_quota_resp *pqr)
                int r = arena_lfs_quota(cmdArrLen, &listOfCmds[0], &pqr);

                printf("block_used: %s \n", pqr.block_used);
                printf("block_soft_limit: %s \n", pqr.block_soft_limit);
                printf("block_hard_limit: %s \n", pqr.block_hard_limit);
                printf("inode_used: %s \n", pqr.inode_used);
                printf("inode_soft_limit: %s \n", pqr.inode_soft_limit);
                printf("inode_hard_limit: %s \n", pqr.inode_hard_limit);

                // cout << "block_used = " << pqr.block_used << endl;
                // cout << "block_soft_limit = " << pqr.block_soft_limit << endl;
                // cout << "block_hard_limit = " << pqr.block_hard_limit << endl;
                // cout << "inode_used = " << pqr.inode_used << endl;
                // cout << "inode_soft_limit = " << pqr.inode_soft_limit << endl;
                // cout << "inode_hard_limit = " << pqr.inode_hard_limit << endl;


                // response json
                rapidjson::StringBuffer strBuf;
                rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
                writer.StartObject();
                writer.Key("code");
                writer.Int(r);
                if (r == 0)
                {
                    writer.Key("msg");
                    writer.String("success");

                    writer.Key("data");
                    writer.StartObject();
                    writer.Key("block_used");
                    writer.String(pqr.block_used);
                    writer.Key("block_soft_limit");
                    writer.String(pqr.block_soft_limit);
                    writer.Key("block_hard_limit");
                    writer.String(pqr.block_hard_limit);
                    writer.Key("inode_used");
                    writer.String(pqr.inode_used);
                    writer.Key("inode_soft_limit");
                    writer.String(pqr.inode_soft_limit);
                    writer.Key("inode_hard_limit");
                    writer.String(pqr.inode_hard_limit);
                    writer.EndObject();
                }
                else
                {
                    writer.Key("msg");
                    writer.String("fail");
                }

                writer.EndObject();

                string jsonStr = strBuf.GetString();

                response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
                response.send(Http::Code::Ok, jsonStr);
            }
            else
            {
                response.send(Http::Code::Internal_Server_Error, "params parse error..");
            }
        }
        else
        {
            response.send(Http::Code::Internal_Server_Error, "request json parse error..");
        }
    }

    void doSetQuota(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto reqBody = request.body();
        rapidjson::Document doc;
        if (!doc.Parse(reqBody.data()).HasParseError())
        {
            if (doc.HasMember("project") && doc["project"].IsUint() 
            && doc.HasMember("mount_point") && doc["mount_point"].IsString() 
            && ((doc.HasMember("block_soft_limit") && doc["block_soft_limit"].IsUint64()) 
            || (doc.HasMember("block_hard_limit") && doc["block_hard_limit"].IsUint64()) 
            || (doc.HasMember("inode_soft_limit") && doc["inode_soft_limit"].IsUint64()) 
            || (doc.HasMember("inode_hard_limit") && doc["inode_hard_limit"].IsUint64())))
            {
                cout << "project = " << doc["project"].GetUint() << endl;
                cout << "mount_point = " << doc["mount_point"].GetString() << endl;

                vector<string> cmdArr;
                cmdArr.push_back("quota");
                cmdArr.push_back("-p");
                cmdArr.push_back(to_string(doc["project"].GetUint()));

                if (doc.HasMember("block_soft_limit") && doc["block_soft_limit"].IsUint64()){
                    cmdArr.push_back("-b");
                    cmdArr.push_back(to_string(doc["block_soft_limit"].GetUint64()));
                    cout << "block_soft_limit = " << doc["block_soft_limit"].GetUint64() << endl;
                }

                if (doc.HasMember("block_hard_limit") && doc["block_hard_limit"].IsUint64()){
                    cmdArr.push_back("-B");
                    cmdArr.push_back(to_string(doc["block_hard_limit"].GetUint64()));
                    cout << "block_hard_limit = " << doc["block_hard_limit"].GetUint64() << endl;
                }

                if (doc.HasMember("inode_soft_limit") && doc["inode_soft_limit"].IsUint64()){
                    cmdArr.push_back("-i");
                    cmdArr.push_back(to_string(doc["inode_soft_limit"].GetUint64()));
                    cout << "inode_soft_limit = " << doc["inode_soft_limit"].GetUint64() << endl;
                }

                if (doc.HasMember("inode_hard_limit") && doc["inode_hard_limit"].IsUint64()){
                    cmdArr.push_back("-I");
                    cmdArr.push_back(to_string(doc["inode_hard_limit"].GetUint64()));
                    cout << "inode_hard_limit = " << doc["inode_hard_limit"].GetUint64() << endl;
                }

                cmdArr.push_back(doc["mount_point"].GetString());

                int cmdArrLen = cmdArr.size();

                char **listOfCmds;
                listOfCmds = new char *[cmdArrLen];

                for (int i = 0; i < cmdArrLen; i++)
                {
                    listOfCmds[i] = &cmdArr[i][0];
                }

                int r = lfs_setquota(cmdArrLen, listOfCmds);

                // response json
                rapidjson::StringBuffer strBuf;
                rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
                writer.StartObject();
                writer.Key("code");
                writer.Int(r);
                if (r == 0)
                {
                    writer.Key("msg");
                    writer.String("success");
                }
                else
                {
                    writer.Key("msg");
                    writer.String("fail");
                }

                writer.EndObject();

                string jsonStr = strBuf.GetString();

                response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
                response.send(Http::Code::Ok, jsonStr);
            }
            else
            {
                response.send(Http::Code::Internal_Server_Error, "params parse error..");
            }
        }
        else
        {
            response.send(Http::Code::Internal_Server_Error, "request json parse error..");
        }
    }

    void doSetProject(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto reqBody = request.body();
        rapidjson::Document doc;
        if (!doc.Parse(reqBody.data()).HasParseError())
        {
            if (doc.HasMember("project") && doc["project"].IsUint()
            && doc.HasMember("file_or_dir") && doc["file_or_dir"].IsString()
             && doc.HasMember("recursion") && doc["recursion"].IsBool()
             && doc.HasMember("assign") && doc["assign"].IsBool()
            )
            {
                cout << "project = " << doc["project"].GetUint() << endl;
                cout << "file_or_dir = " << doc["file_or_dir"].GetString() << endl;
                cout << "recursion = " << doc["recursion"].GetBool() << endl;
                cout << "assign = " << doc["assign"].GetBool() << endl;

                vector<string> cmdArr;
                cmdArr.push_back("");
                cmdArr.push_back("-p");
                cmdArr.push_back(to_string(doc["project"].GetUint()));
                
                if (doc["assign"].GetBool()){
                    cmdArr.push_back("-s");
                }

                if (doc["recursion"].GetBool()){
                    cmdArr.push_back("-r");
                }

                cmdArr.push_back(doc["file_or_dir"].GetString());

                std::vector<char*> argv;
                for (const auto& arg: cmdArr)
                    argv.push_back((char*)arg.data());
                argv.push_back(nullptr);

                int r = lfs_project(argv.size() - 1, argv.data());

                // response json
                rapidjson::StringBuffer strBuf;
                rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
                writer.StartObject();
                writer.Key("code");
                writer.Int(r);
                if (r == 0)
                {
                    writer.Key("msg");
                    writer.String("success");
                }
                else
                {
                    writer.Key("msg");
                    writer.String("fail");
                }

                writer.EndObject();

                string jsonStr = strBuf.GetString();

                response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
                response.send(Http::Code::Ok, jsonStr);
            }
            else
            {
                response.send(Http::Code::Internal_Server_Error, "params parse error..");
            }
        }
        else
        {
            response.send(Http::Code::Internal_Server_Error, "request json parse error..");
        }
    }

    void doClearProject(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto reqBody = request.body();
        rapidjson::Document doc;
        if (!doc.Parse(reqBody.data()).HasParseError())
        {
            if (doc.HasMember("file_or_dir") && doc["file_or_dir"].IsString()
             && doc.HasMember("keep") && doc["keep"].IsBool()
             && doc.HasMember("recursion") && doc["recursion"].IsBool()
            )
            {
                cout << "file_or_dir = " << doc["file_or_dir"].GetString() << endl;
                cout << "keep = " << doc["keep"].GetBool() << endl;
                cout << "recursion = " << doc["recursion"].GetBool() << endl;

                vector<string> cmdArr;
                cmdArr.push_back("project");
                cmdArr.push_back("-C");
                
                if (doc["keep"].GetBool()){
                    cmdArr.push_back("-k");
                }

                if (doc["recursion"].GetBool()){
                    cmdArr.push_back("-r");
                }

                cmdArr.push_back(doc["file_or_dir"].GetString());

                int cmdArrLen = cmdArr.size();

                char **listOfCmds;
                listOfCmds = new char *[cmdArrLen];

                for (int i = 0; i < cmdArrLen; i++)
                {
                    listOfCmds[i] = &cmdArr[i][0];
                }

                int r = lfs_project(cmdArrLen, listOfCmds);

                // response json
                rapidjson::StringBuffer strBuf;
                rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
                writer.StartObject();
                writer.Key("code");
                writer.Int(r);
                if (r == 0)
                {
                    writer.Key("msg");
                    writer.String("success");
                }
                else
                {
                    writer.Key("msg");
                    writer.String("fail");
                }

                writer.EndObject();

                string jsonStr = strBuf.GetString();

                response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
                response.send(Http::Code::Ok, jsonStr);
            }
            else
            {
                response.send(Http::Code::Internal_Server_Error, "params parse error..");
            }
        }
        else
        {
            response.send(Http::Code::Internal_Server_Error, "request json parse error..");
        }
    }

    void doRecordMetric(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto name = request.param(":name").as<std::string>();

        Guard guard(metricsLock);
        auto it = std::find_if(metrics.begin(), metrics.end(), [&](const Metric &metric) {
            return metric.name() == name;
        });

        int val = 1;
        if (request.hasParam(":value"))
        {
            auto value = request.param(":value");
            val = value.as<int>();
        }

        if (it == std::end(metrics))
        {
            metrics.push_back(Metric(std::move(name), val));
            response.send(Http::Code::Created, std::to_string(val));
        }
        else
        {
            auto &metric = *it;
            metric.incr(val);
            response.send(Http::Code::Ok, std::to_string(metric.value()));
        }
    }

    void doGetMetric(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto name = request.param(":name").as<std::string>();

        Guard guard(metricsLock);
        auto it = std::find_if(metrics.begin(), metrics.end(), [&](const Metric &metric) {
            return metric.name() == name;
        });

        if (it == std::end(metrics))
        {
            response.send(Http::Code::Not_Found, "Metric does not exist");
        }
        else
        {
            const auto &metric = *it;
            response.send(Http::Code::Ok, std::to_string(metric.value()));
        }
    }

    void doAuth(const Rest::Request &request, Http::ResponseWriter response)
    {
        std::cout << "what" << std::endl;
        printf("wrong: %s \n", "with you...");
        printCookies(request);
        response.cookies()
            .add(Http::Cookie("lang", "en-US"));
        response.send(Http::Code::Ok);
    }

    class Metric
    {
    public:
        explicit Metric(std::string name, int initialValue = 1)
            : name_(std::move(name)), value_(initialValue)
        {
        }

        int incr(int n = 1)
        {
            int old = value_;
            value_ += n;
            return old;
        }

        int value() const
        {
            return value_;
        }

        const std::string &name() const
        {
            return name_;
        }

    private:
        std::string name_;
        int value_;
    };

    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock metricsLock;
    std::vector<Metric> metrics;

    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

int main(int argc, char *argv[])
{
    Port port(9080);

    int thr = 4;

    if (argc >= 2)
    {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc == 3)
            thr = std::stoi(argv[2]);
    }

    Address addr(Ipv4::any(), port);

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;

    StatsEndpoint stats(addr);

    stats.init(thr);
    stats.start();
}
