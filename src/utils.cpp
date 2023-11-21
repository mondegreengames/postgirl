#include "utils.h"
#include "platform.h"

void readIntFromIni(int& res, FILE* fid) {
    if (fscanf(fid, "\%*s %d", &res) != 1) {
        printf("Error reading int from .ini file\n");
        exit(1);
    }
}

void readStringFromIni(char* buffer, FILE* fid) {
    int str_len, ret;
    if ((ret = fscanf(fid, "%*s (%d): ", &str_len)) != 1) {
        printf("Error reading string from .ini file: %d\n", ret);
        exit(1);
    }
    fread(buffer, sizeof(char), str_len, fid);
    buffer[str_len] = '\0';
}

void printArg(const Argument& arg) {
    printf("name: %s\nvalue: %s\narg_type: %d\n\n", arg.name.buf_, arg.value.buf_, arg.arg_type);
}

void printHistory(const History& hist) {
    printf("url: %s\ninput_json: %s\nresult: %s\n", hist.request.url.buf_, hist.request.input_json.buf_, hist.response.result.buf_);
    printf("req_type: %d\ncontent_type: %d\nresponse_code: %d\n", (int)hist.request.req_type, (int)hist.request.content_type, hist.response.response_code);
    
    char date_buf[128];
    Platform::timestampToIsoString(hist.request.timestamp, date_buf, sizeof(date_buf));
    printf("process_time: %s\n", date_buf);

    for (int i=0; i<hist.request.query_args.size(); i++)
        printArg(hist.request.query_args[i]);
    for (int i=0; i<hist.request.form_args.size(); i++)
        printArg(hist.request.form_args[i]);
    for (int i=0; i<hist.request.headers.size(); i++)
        printArg(hist.request.headers[i]);
    printf("----------------------------------------------------------\n\n");
}

// Thanks lfzawacki: https://stackoverflow.com/questions/3463426/in-c-how-should-i-read-a-text-file-and-print-all-strings/3464656#3464656 
char* readFile(char *filename)
{
   char *buffer = NULL;
   int string_size, read_size;
   FILE *handler = fopen(filename, "r");

   if (handler)
   {
       // Seek the last byte of the file
       fseek(handler, 0, SEEK_END);
       // Offset from the first to the last byte, or in other words, filesize
       string_size = ftell(handler);
       // go back to the start of the file
       rewind(handler);

       // Allocate a string that can hold it all
       buffer = (char*) malloc(sizeof(char) * (string_size + 1) );

       // Read it all in one operation
       read_size = fread(buffer, sizeof(char), string_size, handler);

       // fread doesn't set it so put a \0 in the last position
       // and buffer is now officially a string
       buffer[string_size] = '\0';

       if (string_size != read_size)
       {
           // Something went wrong, throw away the memory and set
           // the buffer to NULL
           free(buffer);
           buffer = NULL;
       }

       // Always remember to close the file.
       fclose(handler);
    }

    return buffer;
}


pg::Vector<History> loadHistory(const pg::String& filename) 
{
    pg::Vector<History> history_vec;
    rapidjson::Document document;
    char* json = readFile(filename.buf_);
    if (json == NULL || document.Parse(json).HasParseError()) {
        return history_vec;
    }
    document.Parse(json);
    if (document.HasMember("histories") == false) {
        free(json);
        return history_vec;
    }

    const rapidjson::Value& histories = document["histories"];

    for (rapidjson::SizeType j = 0; j < histories.Size(); j++) {
        
        History hist;
        hist.request.url = pg::String(histories[j]["url"].GetString());
        hist.request.input_json = pg::String(histories[j]["input_json"].GetString());
        hist.request.req_type = (RequestType)histories[j]["request_type"].GetInt();
        hist.request.content_type = (ContentType)histories[j]["content_type"].GetInt();
        Platform::isoStringToTimestamp(histories[j]["process_time"].GetString(), &hist.request.timestamp);
        hist.response.result = prettify(pg::String(histories[j]["result"].GetString()));
        hist.response.response_code = histories[j]["response_code"].GetInt();
        
        const rapidjson::Value& headers = histories[j]["headers"];
        for (rapidjson::SizeType k = 0; k < headers.Size(); k++) {
            Argument header;
            header.name  = pg::String(headers[k]["name"].GetString());
            header.value = pg::String(headers[k]["value"].GetString());
            header.arg_type = headers[k]["argument_type"].GetInt();
            hist.request.headers.push_back(header);
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
    
    free(json);
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

        curr_history.AddMember("request_type", histories[i].request.req_type, allocator);
        curr_history.AddMember("content_type", histories[i].request.content_type, allocator);
        
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
        for (int k=0; k<histories[i].request.headers.size(); k++) {
            rapidjson::Value curr_header(rapidjson::kObjectType);
            rapidjson::Value name_str;
            name_str.SetString(histories[i].request.headers[k].name.buf_, allocator);
            curr_header.AddMember("name", name_str, allocator);

            rapidjson::Value value_str;
            value_str.SetString(histories[i].request.headers[k].value.buf_, allocator);
            curr_header.AddMember("value", value_str, allocator);

            curr_header.AddMember("argument_type", histories[i].request.headers[k].arg_type, allocator);
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

const char* Stristr(const char* haystack, const char* haystack_end, const char* needle, const char* needle_end)
{
    if (!needle_end)
        needle_end = needle + strlen(needle);

    const char un0 = (char)toupper(*needle);
    while ((!haystack_end && *haystack) || (haystack_end && haystack < haystack_end))
    {
        if (toupper(*haystack) == un0)
        {
            const char* b = needle + 1;
            for (const char* a = haystack + 1; b < needle_end; a++, b++)
                if (toupper(*a) != toupper(*b))
                    break;
            if (b == needle_end)
                return haystack;
        }
        haystack++;
    }
    return NULL;
}

void Help(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}


