#include "history.h"
#include "platform.h"
#include "utils.h"
#include "rapidjson/filereadstream.h"

void printHistory(const History& hist) {
    printf("url: %s\ninput_json: %s\nresult: %s\n", hist.request.url.buf_, hist.request.input_json.buf_, hist.response.result.buf_);
    printf("req_type: %d\nbody_type: %d\nresponse_code: %d\n", (int)hist.request.req_type, (int)hist.request.body_type, hist.response.response_code);
    
    char date_buf[128];
    Platform::timestampToIsoString(hist.request.timestamp, date_buf, sizeof(date_buf));
    printf("process_time: %s\n", date_buf);

    for (int i=0; i<hist.request.query_args.size(); i++)
        printArg(hist.request.query_args[i]);
    for (int i=0; i<hist.request.form_args.size(); i++)
        printArg(hist.request.form_args[i]);
    for (int i=0; i<hist.request.headers.headers.size(); i++)
        printHeader(hist.request.headers.headers[i]);
    printf("----------------------------------------------------------\n\n");
}


bool tryGetInt(const rapidjson::Value& value, const char* name, int* result)
{
    if (value.HasMember(name))
    {
        const auto& ivalue = value[name];
        if (ivalue.IsInt())
        {
            if (result != nullptr) *result = ivalue.GetInt();
            return true;
        }
    }

    return false;
}

bool tryGetString(const rapidjson::Value& value, const char* name, pg::String& result)
{
    if (value.HasMember(name))
    {
        const auto& svalue = value[name];
        if (svalue.IsString())
        {
            result.set(svalue.GetString());
            return true;
        }
    }

    return false;
}

template<typename T>
bool tryGetStringEnum(const rapidjson::Value& value, const char* name, const char* values[], int numValues, T* result)
{
    if (value.HasMember(name))
    {
        const auto& svalue = value[name];
        if (svalue.IsString())
        {
            auto str = svalue.GetString();
            for (int i = 0; i < numValues; i++)
            {
                if (strcmp(values[i], str) == 0)
                {
                    if (result != nullptr) *result = (T)i;
                    return true;
                }
            }
        }
    }

    return false;
}



pg::Vector<History> loadHistory(const pg::String& filename) 
{
    pg::Vector<History> history_vec;
    rapidjson::Document document;

    FILE *fp = fopen(filename.buf_, "rb");
    if (fp == nullptr) return history_vec;

    char readBuffer[1024 * 64];
    rapidjson::FileReadStream stream(fp, readBuffer, sizeof(readBuffer));

    if (document.ParseStream(stream).HasParseError())
    {
        fclose(fp);
        return history_vec;
    }

    if (document.HasMember("histories") == false) {
        fclose(fp);
        return history_vec;
    }

    const rapidjson::Value& histories = document["histories"];

    for (rapidjson::SizeType j = 0; j < histories.Size(); j++) {
        
        History hist;
        hist.request.url = pg::String(histories[j]["url"].GetString());
        hist.request.input_json = pg::String(histories[j]["input_json"].GetString());
        if (tryGetStringEnum(histories[j], "request_type", requestTypeStrings, requestTypeStringsLength, &hist.request.req_type) == false)
            hist.request.req_type = RequestType::GET;
        if (tryGetStringEnum(histories[j], "body_type", bodyTypeStrings, bodyTypeStringsLength, &hist.request.body_type) == false)
            hist.request.body_type = BodyType::MULTIPART_FORMDATA;
        Platform::isoStringToTimestamp(histories[j]["process_time"].GetString(), &hist.request.timestamp);
        hist.response.result = prettify(pg::String(histories[j]["result"].GetString()));
        hist.response.response_code = histories[j]["response_code"].GetInt();
        
        const rapidjson::Value& headers = histories[j]["headers"];
        for (rapidjson::SizeType k = 0; k < headers.Size(); k++) {
            HeaderKeyValue header;
            header.key  = pg::String(headers[k]["name"].GetString());
            header.value = pg::String(headers[k]["value"].GetString());
            header.enabled = true;// FIXME
            hist.request.headers.headers.push_back(header);
        }

        // Parse the "arguments" collection to maintain compatibility
        auto arguments_itr = histories[j].FindMember("arguments");
        if (arguments_itr != histories[j].MemberEnd())
        {
            auto& arguments = (*arguments_itr).value;
            for (rapidjson::SizeType k = 0; k < arguments.Size(); k++) {
                Argument arg;
                arg.name  = pg::String(arguments[k]["name"].GetString());
                arg.value = pg::String(arguments[k]["value"].GetString());
                arg.arg_type = arguments[k]["argument_type"].GetInt();

                if (arg.arg_type == 1)
                    hist.request.form_args.push_back(arg);
                else
                    hist.request.query_args.push_back(arg);
            }
        }

        auto query_arguments_itr = histories[j].FindMember("query_arguments");
        if (query_arguments_itr != histories[j].MemberEnd())
        {
            auto& query_arguments = (*query_arguments_itr).value;
            for (rapidjson::SizeType k = 0; k < query_arguments.Size(); k++) {
                Argument arg;
                arg.name  = pg::String(query_arguments[k]["name"].GetString());
                arg.value = pg::String(query_arguments[k]["value"].GetString());
                arg.arg_type = 0;

                hist.request.query_args.push_back(arg);
            }
        }

        auto form_arguments_itr = histories[j].FindMember("form_arguments");
        if (form_arguments_itr != histories[j].MemberEnd())
        { 
            auto& form_arguments = (*form_arguments_itr).value;
            for (rapidjson::SizeType k = 0; k < form_arguments.Size(); k++) {
                Argument arg;
                arg.name  = pg::String(form_arguments[k]["name"].GetString());
                arg.value = pg::String(form_arguments[k]["value"].GetString());
                arg.arg_type = form_arguments[k]["argument_type"].GetInt();;

                hist.request.form_args.push_back(arg);
            }
        }

        // printHistory(hist);
        history_vec.push_back(hist);
    }
    
    fclose(fp);
    return history_vec;
}

void saveHistory(const pg::Vector<History>& histories, const pg::String& filename, bool pretty) 
{
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Value history_array(rapidjson::kArrayType);
 
	// must pass an allocator when the object may need to allocate memory
	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    for (int i=0; i<histories.size(); i++) {
        rapidjson::Value curr_history(rapidjson::kObjectType);
        rapidjson::Value url_str;
        url_str.SetString(histories[i].request.url.buf_, allocator);
        curr_history.AddMember("url", url_str, allocator);
        
        rapidjson::Value input_json_str;
        input_json_str.SetString(histories[i].request.input_json.buf_, allocator);
        curr_history.AddMember("input_json", input_json_str, allocator);

        rapidjson::Value request_type_str;
        request_type_str.SetString(RequestTypeToString(histories[i].request.req_type), allocator);
        curr_history.AddMember("request_type", request_type_str, allocator);

        rapidjson::Value body_type_str;
        body_type_str.SetString(BodyTypeToString(histories[i].request.body_type), allocator);
        curr_history.AddMember("body_type", body_type_str, allocator);
        
        rapidjson::Value process_time_str;
        char buffer[128];
        Platform::timestampToIsoString(histories[i].request.timestamp, buffer, sizeof(buffer));
        process_time_str.SetString(buffer, allocator);
        curr_history.AddMember("process_time", process_time_str, allocator);
        
        rapidjson::Value result_str;
        result_str.SetString(histories[i].response.result.buf_, allocator);
        curr_history.AddMember("result", result_str, allocator);
        
        curr_history.AddMember("response_code", histories[i].response.response_code, allocator);


        rapidjson::Value query_args_array(rapidjson::kArrayType);
        for (int k=0; k<histories[i].request.query_args.size(); k++) {
            rapidjson::Value curr_arg(rapidjson::kObjectType);
            rapidjson::Value name_str;
            name_str.SetString(histories[i].request.query_args[k].name.buf_, allocator);
            curr_arg.AddMember("name", name_str, allocator);

            rapidjson::Value value_str;
            value_str.SetString(histories[i].request.query_args[k].value.buf_, allocator);
            curr_arg.AddMember("value", value_str, allocator);

            query_args_array.PushBack(curr_arg, allocator);
        }
        curr_history.AddMember("query_arguments", query_args_array, allocator);

        rapidjson::Value form_args_array(rapidjson::kArrayType);
        for (int k=0; k<histories[i].request.form_args.size(); k++) {
            rapidjson::Value curr_arg(rapidjson::kObjectType);
            rapidjson::Value name_str;
            name_str.SetString(histories[i].request.form_args[k].name.buf_, allocator);
            curr_arg.AddMember("name", name_str, allocator);

            rapidjson::Value value_str;
            value_str.SetString(histories[i].request.form_args[k].value.buf_, allocator);
            curr_arg.AddMember("value", value_str, allocator);

            curr_arg.AddMember("argument_type", histories[i].request.form_args[k].arg_type, allocator);
            form_args_array.PushBack(curr_arg, allocator);
        }
        curr_history.AddMember("form_arguments", form_args_array, allocator);

        rapidjson::Value headers_array(rapidjson::kArrayType);
        for (int k=0; k<histories[i].request.headers.headers.size(); k++) {
            rapidjson::Value curr_header(rapidjson::kObjectType);
            rapidjson::Value name_str;
            name_str.SetString(histories[i].request.headers.headers[k].key.buf_, allocator);
            curr_header.AddMember("name", name_str, allocator);

            rapidjson::Value value_str;
            value_str.SetString(histories[i].request.headers.headers[k].value.buf_, allocator);
            curr_header.AddMember("value", value_str, allocator);

            curr_header.AddMember("enabled", histories[i].request.headers.headers[k].enabled, allocator);
            headers_array.PushBack(curr_header, allocator);
        }
        curr_history.AddMember("headers", headers_array, allocator);

        history_array.PushBack(curr_history, allocator);
    }
    document.AddMember("histories", history_array, allocator);
 
    rapidjson::StringBuffer strbuf;

    if (pretty)
    {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
        document.Accept(writer);
    }
    else
    {
        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
        document.Accept(writer);
    }

    FILE *fp = fopen(filename.buf_, "wb");
    if (fp != NULL)
    {
        fputs(strbuf.GetString(), fp);
        fclose(fp);
    }
}
