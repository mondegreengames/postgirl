#include <thread>
#include <atomic>
#include <stdio.h>
#include <ctype.h> // toupper
#include <sys/time.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "GL/gl3w.h"    // This example is using gl3w to access OpenGL functions (because it is small). You may use glew/glad/glLoadGen/etc. whatever already works for you.
#include <GLFW/glfw3.h>
#include "pgstring.h"
#include "pgvector.h"
#include "rapidjson/document.h"
#include "dirent_portable.h"
#include "requests.h"
#include "utils.h"

#ifdef _WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif



// Defines the current selected request. It may the current one or
// another, from the history list.
int selected  = 0;

void processRequest(std::thread& thread, const char* buf, 
                    pg::Vector<History>& history, const pg::Vector<Argument>& args, 
                    const pg::Vector<Argument>& headers, int request_type, 
                    ContentType contentType, const pg::String& inputJson,
                    std::atomic<ThreadStatus>& thread_status)
{
    if (thread_status != IDLE)
        return;
    History hist;
    hist.url = pg::String(buf);
    hist.args = args;
    hist.headers = headers;
    hist.input_json = inputJson;
    if (request_type == GET || request_type == DELETE || contentType != APPLICATION_JSON)
        hist.input_json = pg::String("");
    hist.result = pg::String("Processing");
    hist.content_type = contentType;
    hist.req_type = (RequestType)request_type;
    time_t t = time(NULL);
    struct tm* ptm = gmtime(&t);
    char date_buf[128];
    if (strftime(date_buf, 128, "%B %d, %Y; %H:%M:%S\n", ptm) != 0)
        hist.process_time = pg::String(date_buf);
    else
        hist.process_time = pg::String("");
    history.push_back(hist);
    // points to the current (and unfinished) request
    selected = (int)history.size()-1;
    
    thread_status = RUNNING;

    switch(request_type) { 
        case GET:
        case DELETE:
            thread = std::thread(threadRequestGetDelete, std::ref(thread_status), (RequestType)request_type, history.back().url, history.back().args, history.back().headers, contentType, std::ref(history.back().result), std::ref(history.back().response_code));
            break;
        case POST:
        case PATCH:
        case PUT:
            thread = std::thread(threadRequestPostPatchPut, std::ref(thread_status), (RequestType)request_type, history.back().url, history.back().args, history.back().headers, contentType, history.back().input_json, std::ref(history.back().result), std::ref(history.back().response_code));
            break;
        default:
            history.back().result = pg::String("Invalid request type selected!");
            thread_status = FINISHED;
    }
}


int compareSize(const void* A, const void* B)
{
    return strcmp(((pg::String*)A)->buf_, ((pg::String*)B)->buf_);
}


static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Error %d: %s\n", error, description);
}

static inline float GetWindowContentRegionWidth()
{
    return ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x; 
}

int main(int argc, char* argv[])
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Decide GL+GLSL versions
#if __APPLE__
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Postgirl", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
    bool err = gl3wInit() != 0;
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Our state
    ImVec4 clear_color = ImVec4(0.1f, 0.1f, 0.1f, 1.00f);

    std::atomic<ThreadStatus> thread_status(IDLE); // syncronize thread to call join only when it finishes
    std::thread thread;

    // Such ugly code... Can I do something like this?
    // char** arg_types[] = { {"Text", "File"}, "Text" }; 
    pg::Vector<const char**> arg_types;
    const char* post_types[] = {"Text", "File"};
    const char* get_types[] = {"Text"};
    const char* delete_types[] = {"Text"};
    const char* patch_types[] = {"Text", "File"};
    const char* put_types[] = {"Text", "File"};
    arg_types.push_back(get_types);
    arg_types.push_back(post_types);
    arg_types.push_back(delete_types);
    arg_types.push_back(patch_types);
    arg_types.push_back(put_types);
    pg::Vector<int> num_arg_types;
    num_arg_types.push_back(1);
    num_arg_types.push_back(2);
    num_arg_types.push_back(1);
    num_arg_types.push_back(2);
    num_arg_types.push_back(2);

    bool picking_file = false;
    bool show_history = true;
    int curr_arg_file = 0;
    

    pg::Vector<Collection> collection = loadCollection("collections.json");
    if (collection.size() == 0) {
        Collection temp_col;
        collection.push_back(temp_col);
    }
    int curr_history = 0;
    int curr_collection = 0;
    bool update_hist_search = true; // used to init stuff on first run
    curl_global_init(CURL_GLOBAL_ALL);

    pg::Vector<pg::String> content_type_str;
    content_type_str.push_back(ContentTypeToString(MULTIPART_FORMDATA));
    content_type_str.push_back(ContentTypeToString(APPLICATION_JSON));
    
    pg::Vector<pg::String> request_type_str;
    for (int i=0; i<5; i++) {
        request_type_str.push_back(RequestTypeToString((RequestType)i));
    }

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        long start, end;
        struct timeval timecheck;

        gettimeofday(&timecheck, NULL);
        start = (long)timecheck.tv_sec * 1000 + (long)timecheck.tv_usec / 1000;

        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        static const char* items[] = {"GET", "POST", "DELETE", "PATCH", "PUT"};
        static const char* ct_post[] = {"multipart/form-data", "application/json", "<NONE>"};
        static int request_type = 0;
        static ContentType content_type = (ContentType)0;
        static pg::Vector<Argument> headers;
        static pg::String result;
        static pg::Vector<Argument> args;
        static pg::String input_json(1024*3200); // 32KB static string should be reasonable
        static char url_buf[4098] = "http://localhost:5000/test_route";

        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
        ImGui::Begin("Postgirl", NULL, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_HorizontalScrollbar);

        {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            ImGui::BeginChild("History", ImVec2(GetWindowContentRegionWidth() * 0.2f, 0), false, window_flags);

            ImGui::Text("History Search");
            if (ImGui::BeginMenuBar()) {
                for (int i=(int)collection.size()-1; i>=0; i--) {
                    if (ImGui::BeginMenu(collection[i].name.buf_)) {
                        curr_collection = i;
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenuBar();
            }

            static pg::String hist_search;
            static pg::Vector<int> search_result;
            static bool live_search = true;
            static ImGuiInputTextFlags search_flags = 0; 

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.95);
            if (ImGui::InputText("##Search", hist_search.buf_, hist_search.capacity(), search_flags) || update_hist_search)
            {
                update_hist_search = false;
                search_result.clear();
                if (hist_search.length() > 0) {
                    char *fb = hist_search.buf_, *fe = hist_search.end();
                    for (int i=(int)collection[curr_collection].hist.size()-1; i>=0; i--) {
                        if (hist_search.length() == 0 || (hist_search.length() > 0 &&
                            (Stristr(collection[curr_collection].hist[i].url.buf_, collection[curr_collection].hist[i].url.end(), fb, fe) ||
                            Stristr(collection[curr_collection].hist[i].input_json.buf_, collection[curr_collection].hist[i].input_json.end(), fb, fe) ||
                            Stristr(collection[curr_collection].hist[i].result.buf_, collection[curr_collection].hist[i].result.end(), fb, fe))))
                        {
                            search_result.push_back(i);
                        }
                    }
                }
                else {
                    for (int i=(int)collection[curr_collection].hist.size()-1; i>=0; i--) {
                        search_result.push_back(i);
                    }
                }
            }
            if (ImGui::Checkbox("Live Search", &live_search)) {
                if (search_flags) {
                    search_flags = 0;
                }
                else {
                    search_flags = ImGuiInputTextFlags_EnterReturnsTrue;
                }
            }
            ImGui::SameLine(); Help("Live Search can be slow depending on your history size!");

            ImGui::BeginChild("HistoryList", ImVec2(GetWindowContentRegionWidth(), 0), false, window_flags);
            char *fb = hist_search.buf_, *fe = hist_search.end();
            for (int sr=0; sr<search_result.size(); sr++) {
                int i = search_result[sr];
                char select_name[2048];
                sprintf(select_name, "(%s) %s##%d", request_type_str[(int)collection[curr_collection].hist[i].req_type].buf_, collection[curr_collection].hist[i].url.buf_, i);
                if (ImGui::Selectable(select_name, selected==i)) {
                    selected = i;
                    request_type = collection[curr_collection].hist[i].req_type;
                    content_type = collection[curr_collection].hist[i].content_type;
                    headers = collection[curr_collection].hist[i].headers;
                    result = collection[curr_collection].hist[i].result;
                    args = collection[curr_collection].hist[i].args;
                    input_json = collection[curr_collection].hist[i].input_json;
                    strcpy(url_buf, collection[curr_collection].hist[i].url.buf_);
                }
            }
            ImGui::EndChild();
            ImGui::EndChild();
        }

        ImGui::SameLine();

        {
            // ImGui::Begin("Postgirl");//, NULL, ImGuiWindowFlags_MenuBar );
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            ImGui::BeginChild("MainMenu", ImVec2(0, 0), false, window_flags);

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.125);
            if (ImGui::BeginCombo("##request_type", items[request_type])) {
                for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
                    if (ImGui::Selectable(items[n])) {
                        request_type = n;
                        content_type = MULTIPART_FORMDATA;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();

            switch (request_type) {
                case GET:
                case DELETE: break;
                case POST:
                case PATCH:
                case PUT:
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.25);
                    if (ImGui::BeginCombo("##content_type", ct_post[(int)content_type])) {
                        for (int n = 0; n < IM_ARRAYSIZE(ct_post); n++) {
                            if (ImGui::Selectable(ct_post[n])) {
                                content_type = (ContentType)n;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    break;
            }
            
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputText("##URL", url_buf, IM_ARRAYSIZE(url_buf), ImGuiInputTextFlags_EnterReturnsTrue) ) {
                ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
                processRequest(thread, url_buf, collection[curr_collection].hist, args, headers, request_type, content_type, input_json, thread_status);
            }


            static pg::Vector<int> delete_arg_btn;
            for (int i=0; i<(int)headers.size(); i++) {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.2);
                char arg_name[32];
                sprintf(arg_name, "Name##header arg name%d", i);
                if (ImGui::InputText(arg_name, &headers[i].name[0], headers[i].name.capacity(), ImGuiInputTextFlags_EnterReturnsTrue))
                    processRequest(thread, url_buf, collection[curr_collection].hist, args, headers, request_type, content_type, input_json, thread_status);
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.4);
                sprintf(arg_name, "Value##header arg value%d", i);
                if (ImGui::InputText(arg_name, &headers[i].value[0], headers[i].value.capacity(), ImGuiInputTextFlags_EnterReturnsTrue))
                    processRequest(thread, url_buf, collection[curr_collection].hist, args, headers, request_type, content_type, input_json, thread_status);
                ImGui::SameLine();
                char btn_name[32];
                sprintf(btn_name, "Delete##header arg delete%d", i);
                if (ImGui::Button(btn_name)) {
                    delete_arg_btn.push_back(i);
                }
            }
            
            // delete headers
            for (int i=(int)delete_arg_btn.size(); i>0; i--) {
                headers.erase(headers.begin()+delete_arg_btn[i-1]);
            }
            delete_arg_btn.clear();

            if (ImGui::Button("Add Header Arg")) {
                headers.push_back(Argument());
            }

            for (int i=0; i<(int)args.size(); i++) {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.2);
                char combo_name[32];
                sprintf(combo_name, "##combo arg type%d", i);
                ImGui::Combo(combo_name, &args[i].arg_type, arg_types[request_type], num_arg_types[request_type]);
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.2);
                char arg_name[32];
                sprintf(arg_name, "Name##arg name%d", i);
                if (ImGui::InputText(arg_name, &args[i].name[0], args[i].name.capacity(), ImGuiInputTextFlags_EnterReturnsTrue))
                    processRequest(thread, url_buf, collection[curr_collection].hist, args, headers, request_type, content_type, input_json, thread_status);
                ImGui::SameLine();
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.6);
                sprintf(arg_name, "Value##arg name%d", i);
                if (ImGui::InputText(arg_name, &args[i].value[0], args[i].value.capacity(), ImGuiInputTextFlags_EnterReturnsTrue))
                    processRequest(thread, url_buf, collection[curr_collection].hist, args, headers, request_type, content_type, input_json, thread_status);
                ImGui::SameLine();
                if (args[i].arg_type == 1) {
                    sprintf(arg_name, "File##arg name%d", i);
                    if (ImGui::Button(arg_name)) {
                        picking_file = true;
                        curr_arg_file = i;
                    }
                }
                ImGui::SameLine();
                char btn_name[32];
                sprintf(btn_name, "Delete##arg delete%d", i);
                if (ImGui::Button(btn_name)) {
                    if (curr_arg_file == i) {
                        curr_arg_file = -1;
                        picking_file = false;
                    }
                    delete_arg_btn.push_back(i);
                }
            }
            
            if (ImGui::Button("Add Argument")) {
                Argument arg;
                arg.arg_type = 0;
                args.push_back(arg);
            }
            ImGui::SameLine();
            
            // delete the args
            if (thread_status != RUNNING) {
                for (int i=(int)delete_arg_btn.size(); i>0; i--) {
                    args.erase(args.begin()+delete_arg_btn[i-1]);
                }
            }
            delete_arg_btn.clear();
            if (ImGui::Button("Delete all args") && thread_status != RUNNING) {
                args.clear();
            }

            if (thread_status == FINISHED) {
                thread.join();
                thread_status = IDLE;
                selected = (int)collection[curr_collection].hist.size()-1;
                saveCollection(collection, "collections.json");
                update_hist_search = true;
            }
            
            if ((request_type == POST || request_type == PUT || request_type == PATCH) && content_type == 1) {
                ImGui::Text("Input JSON");
                rapidjson::Document d;
                // TODO: only check for JSON errors on changes instead of every frame
                if (d.Parse(input_json.buf_).HasParseError() && input_json.length() > 0) {
                    ImGui::SameLine();
                    ImGui::Text("Problems with JSON");
                }
                int block_height = ImGui::GetContentRegionAvail()[1];
                block_height /= 2;
                ImGui::InputTextMultiline("##input_json", &input_json[0], input_json.capacity(), ImVec2(-1.0f, block_height), ImGuiInputTextFlags_AllowTabInput);
            }

            ImGui::Text("Result");
            if (collection[curr_collection].hist.size() > 0) {
                if (selected >= collection[curr_collection].hist.size()) {
                    selected = (int)collection[curr_collection].hist.size()-1;
                }
                ImGui::InputTextMultiline("##source", &collection[curr_collection].hist[selected].result[0], collection[curr_collection].hist[selected].result.capacity(), ImVec2(-1.0f, ImGui::GetContentRegionAvail()[1]), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_ReadOnly);
            }
            else {
                char blank[] = "";
                ImGui::InputTextMultiline("##source", blank, 0, ImVec2(-1.0f, ImGui::GetContentRegionAvail()[1]), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_ReadOnly);
            }


            ImGui::EndChild();
        }
        if (picking_file) {
            ImGui::Begin("File Selector", &picking_file);
            static pg::Vector<pg::String> curr_files;
            static pg::Vector<pg::String> curr_folders;
            static pg::String curr_dir(".");
            if (curr_files.size() == 0 && curr_folders.size()==0) {
                DIR *dir;
                dirent *pdir;
                dir=opendir(curr_dir.buf_);
                while((pdir=readdir(dir))) {
                    if (pdir->d_type == DT_DIR) {
                        curr_folders.push_back(pdir->d_name);
                    } else if (pdir->d_type == DT_REG) {
                        curr_files.push_back(pdir->d_name);
                    }
                }
                pg::String aux_dir = curr_dir;
                if (curr_dir.capacity_ < 2048) curr_dir.realloc(2048);
                realpath(aux_dir.buf_, curr_dir.buf_);
                closedir(dir);

                qsort(curr_folders.begin(), curr_folders.size(), sizeof(*curr_folders.begin()), compareSize);
                qsort(curr_files.begin(), curr_files.size(), sizeof(*curr_files.begin()), compareSize);
            }

            
            // ImGui::Text(curr_dir.buf_);
            ImGui::Button(curr_dir.buf_);
            for (int i=0; i<curr_folders.size(); i++) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + ImGui::GetContentRegionAvail()[0], pos.y + ImGui::GetTextLineHeight()), IM_COL32(100,0,255,50));
                if (ImGui::MenuItem(curr_folders[i].buf_, NULL)) {
                    curr_dir.append("/");
                    curr_dir.append(curr_folders[i]);
                    curr_files.clear();
                    curr_folders.clear();
                }
            }
            for (int i=0; i<curr_files.size(); i++) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + ImGui::GetContentRegionAvail()[0], pos.y + ImGui::GetTextLineHeight()), IM_COL32(100,100,0,50));
                if (ImGui::MenuItem(curr_files[i].buf_, NULL)) {
                    if (curr_arg_file >= 0 && curr_arg_file < args.size()) {
                        pg::String filename = curr_dir;
                        filename.append("/");
                        filename.append(curr_files[i]);
                        args[curr_arg_file].value = filename;
                    }                    

                    picking_file = false;
                    curr_arg_file = -1;
                }
            }
            ImGui::End();
        }

        ImGui::End();
        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        gettimeofday(&timecheck, NULL);
        end = (long)timecheck.tv_sec * 1000 + (long)timecheck.tv_usec / 1000;
        long sleep_time = 1000/60-(end-start);
        if (sleep_time > 0) {
            Sleep(sleep_time);
        }
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

