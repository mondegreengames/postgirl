#include <thread>
#include <atomic>
#include <stdio.h>
#include <ctype.h> // toupper
#include "imgui.h"
#include "imgui_internal.h"
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
#include "settings.h"
#include "collection.h"
#include "history.h"
#include "platform.h"
#include "dynamicBitSet.h"
#include "collectionTree.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif

// Defines the current selected request. It may the current one or
// another, from the history list.
int selected  = 0;

void processRequest(std::thread& thread,
                    pg::Vector<History>& history, const Request& currentRequest,
                    std::atomic<ThreadStatus>& thread_status)
{
    if (thread_status != IDLE)
        return;
    History hist;
    hist.request.url = currentRequest.url;
    hist.request.query_args = currentRequest.query_args;
    hist.request.form_args = currentRequest.form_args;
    hist.request.headers = currentRequest.headers;
    hist.request.input_json = currentRequest.input_json;
    if (currentRequest.req_type == RequestType::GET || currentRequest.req_type == RequestType::DELETE || currentRequest.body_type != BodyType::RAW)
        hist.request.input_json = pg::String("");
    hist.response.result = pg::String("Processing");
    hist.request.body_type = currentRequest.body_type;
    hist.request.req_type = currentRequest.req_type;
    hist.request.timestamp = Platform::getUtcTimestampNow();
    history.push_back(hist);
    // points to the current (and unfinished) request
    selected = (int)history.size()-1;
    
    thread_status = RUNNING;

    auto& new_history = history.back(); 
    switch(currentRequest.req_type) { 
        case RequestType::GET:
        case RequestType::DELETE:
        case RequestType::OPTIONS:
            thread = std::thread(threadRequestGetDelete, 
                std::ref(thread_status), 
                currentRequest.req_type, 
                new_history.request.url, 
                new_history.request.query_args, 
                new_history.request.headers.headers, 
                currentRequest.body_type, 
                std::ref(new_history.response.result), 
                std::ref(new_history.response.result_headers.headers), 
                std::ref(new_history.response.response_code));
            break;
        case RequestType::POST:
        case RequestType::PATCH:
        case RequestType::PUT:
            thread = std::thread(threadRequestPostPatchPut, 
                std::ref(thread_status), 
                currentRequest.req_type, 
                new_history.request.url, 
                new_history.request.query_args, 
                new_history.request.form_args, 
                new_history.request.headers.headers, 
                currentRequest.body_type, 
                new_history.request.input_json, 
                std::ref(new_history.response.result), 
                std::ref(new_history.response.result_headers.headers), 
                std::ref(new_history.response.response_code));
            break;
        default:
            history.back().response.result = pg::String("Invalid request type selected!");
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

int renderCollections(CollectionTree& tree, int currentSelectedId)
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();

    int selectedNodeId = 0;
    int dirtyFlag = CollectionTree::InvalidIndex;
    bool selected = false;

    static pg::Vector<int> stack;
    stack.resize(0);

    int i = 0;
    while(i < tree.nodes.Size) {

        const CollectionNode* itr = &tree.nodes[i];
        
        while(stack.Size > 0 && itr->parentIndex != stack.back()) {
            ImGui::TreePop();
            stack.pop_back();
        }

        const char* name;
        if (itr->nameIndex == CollectionTree::InvalidIndex) {
            name = "Unnamed";
        }
        else {
            name = tree.names[itr->nameIndex].buf_;
        }

        const char* dirtyIndicator = itr->isDirty ? "*" : "";

        bool nodeOpen;
        if (itr->requestIndex != CollectionTree::InvalidIndex) {
            const int totalWidth = ImGui::GetWindowWidth();
            const int frameHeight = ImGui::GetFrameHeight();

            Request* req = &tree.requests[itr->requestIndex];
            const bool selected = currentSelectedId == itr->id;
            const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowItemOverlap | (selected ? ImGuiTreeNodeFlags_Selected : 0);
            nodeOpen = ImGui::TreeNodeEx(itr, flags, "%s %s%s", RequestTypeToString(req->req_type), name, dirtyIndicator);

            if (ImGui::IsItemClicked()) {
                selectedNodeId = itr->id;

                ctx->InputTextDeactivatedState.ID = 0; // workaround for the url textbox overwriting this value
            }
        }
        else {
            const bool selected = currentSelectedId == itr->id;
            nodeOpen = ImGui::TreeNodeEx(itr, ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | (selected ? ImGuiTreeNodeFlags_Selected : 0), "%s%s", name, dirtyIndicator);
            if (ImGui::IsItemClicked()) {
                selectedNodeId = itr->id;
            }
        }

        if (nodeOpen == false) {
            i += itr->numDescendants + 1;
        }
        else {
            stack.push_back(i);
            i++;
        }
    }

    while(stack.Size > 0) {
        ImGui::TreePop();
        stack.pop_back();
    }

    return selectedNodeId;
}

#define MAKE_TREE_NODE_DIRTY(index) if (index != CollectionTree::InvalidIndex) { tree.setDirty(index, true); }

#if 0
const Request* renderCollections(const Item& item, const Request* originalSelectedRequest)
{
    const Request* selectedRequest = nullptr;

    const char* name = "Unnamed";
    if (item.name.buf_[0] != 0)
        name = item.name.buf_;

    if (item.data.index() == 0)
    {
        auto& items = std::get<pg::Vector<Item>>(item.data);
        if (ImGui::TreeNode(name))
        {
            for (auto itr = items.begin(); itr != items.end(); ++itr)
            {
                const Request* renderResult = renderCollections((*itr), originalSelectedRequest);
                if (renderResult != nullptr)
                    selectedRequest = renderResult;
            }

            ImGui::TreePop();
        }
    }
    else
    {
        auto& request = std::get<Request>(item.data);
        
        bool selected = &request == originalSelectedRequest;
        if (ImGui::TreeNodeEx(&item, ImGuiTreeNodeFlags_Leaf | (selected ? ImGuiTreeNodeFlags_Selected : 0), "%s %s", RequestTypeToString(request.req_type), name))
        {
            if (ImGui::IsItemClicked())
                selectedRequest = &request;

            ImGui::TreePop();
        }
    }

    return selectedRequest;
}
#endif

int main(int argc, char* argv[])
{
    DynamicBitSet::test();

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

    Settings settings;
    Settings::Load("settings.json", settings);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    if (settings.Font.buf_[0] != 0)
    {
        io.Fonts->AddFontFromFileTTF(settings.Font.buf_, settings.FontSize == 0 ? 14 : settings.FontSize);
    }
    io.Fonts->AddFontDefault();

    
    if (settings.Theme == ThemeType::CLASSIC)
        ImGui::StyleColorsClassic();
    else if (settings.Theme == ThemeType::LIGHT)
        ImGui::StyleColorsLight();
    else
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
   // bool show_history = true;
    int curr_arg_file = 0;

    pg::Vector<History> histories = loadHistory("history.json");
    if (histories.size() == 0) {
        History temp_col;
        histories.push_back(temp_col);
    }
    bool update_hist_search = true; // used to init stuff on first run

    Collection collection;
    Collection::Load("collections.json", collection);

    CollectionTree tree;
    buildTreeFromCollection(collection, tree);

    curl_global_init(CURL_GLOBAL_ALL);
    
    //pg::Vector<pg::String> request_type_str;
    //for (int i=0; i<5; i++) {
    //    request_type_str.push_back(RequestTypeToString((RequestType)i));
    //}

    Request workingRequest;
    workingRequest.url.resize(4098);
    workingRequest.input_json.resize(1024*3200);
    workingRequest.url.set("http://localhost:5000/test_route");
    Request* currentRequest = nullptr;
    int selectedNodeId = 0;

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        uint64_t start, end;
        start = Platform::getUtcTimestampNow();

        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        //static const char* items[] = {"GET", "POST", "DELETE", "PATCH", "PUT"};
        //static const char* ct_post[] = {"multipart/form-data", "application/json", "<NONE>"};



        //static int request_type = 0;
        //static ContentType content_type = (ContentType)0;
        //static pg::Vector<Argument> headers;
        static pg::String result;
        //static pg::Vector<Argument> query_args;
        //static pg::Vector<Argument> form_args;
        //static pg::String input_json(1024*3200); // 32KB static string should be reasonable
        //constexpr int url_buf_capacity = 4098;
        //static char url_buf[url_buf_capacity] = "http://localhost:5000/test_route";


        bool settingsDirty = false;
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::BeginMenu("Settings"))
                {
                    if (ImGui::MenuItem("Pretty-print collections file", NULL, settings.PrettifyCollectionsJson))
                    {
                        settings.PrettifyCollectionsJson = !settings.PrettifyCollectionsJson;
                        settingsDirty = true;
                    }
                    if (ImGui::BeginMenu("Theme"))
                    {
                        if (ImGui::MenuItem("Dark", NULL, settings.Theme == ThemeType::DARK))
                        {
                            ImGui::StyleColorsDark();
                            settings.Theme = ThemeType::DARK;
                            settingsDirty = true;
                        }
                        if (ImGui::MenuItem("Light", NULL, settings.Theme == ThemeType::LIGHT))
                        {
                            ImGui::StyleColorsLight();
                            settings.Theme = ThemeType::LIGHT;
                            settingsDirty = true;
                        }
                        if (ImGui::MenuItem("Classic", NULL, settings.Theme == ThemeType::CLASSIC))
                        {
                            ImGui::StyleColorsClassic();
                            settings.Theme = ThemeType::CLASSIC;
                            settingsDirty = true;
                        }

                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Exit"))
                {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
        if (settingsDirty)
            Settings::Save("settings.json", settings);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##Postgirl", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_HorizontalScrollbar);

        ImGui::BeginChild("leftpane", ImVec2(GetWindowContentRegionWidth() * 0.2f, 0), ImGuiChildFlags_ResizeX, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::BeginTabBar("historytabs");
        if (ImGui::BeginTabItem("History"))
        {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;

            ImGui::Text("History Search");

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
                    for (int i=(int)histories.size()-1; i>=0; i--) {
                        if (hist_search.length() == 0 || (hist_search.length() > 0 &&
                            (Stristr(histories[i].request.url.buf_, histories[i].request.url.end(), fb, fe) ||
                            Stristr(histories[i].request.input_json.buf_, histories[i].request.input_json.end(), fb, fe) ||
                            Stristr(histories[i].response.result.buf_, histories[i].response.result.end(), fb, fe))))
                        {
                            search_result.push_back(i);
                        }
                    }
                }
                else {
                    for (int i=(int)histories.size()-1; i>=0; i--) {
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
            //char *fb = hist_search.buf_, *fe = hist_search.end();
            for (int sr=0; sr<search_result.size(); sr++) {
                int i = search_result[sr];
                char select_name[2048];
                sprintf(select_name, "(%s) %s##%d", requestTypeStrings[(int)histories[i].request.req_type], histories[i].request.url.buf_, i);
                if (ImGui::Selectable(select_name, selected==i)) {
                    selected = i;
                    workingRequest = histories[i].request;
                    currentRequest = &workingRequest;
                    selectedNodeId = CollectionTree::InvalidId;
                    //currentRequest.req_type = histories[i].request.req_type;
                    //currentRequest.content_type = histories[i].request.content_type;
                    //currentRequest.headers = histories[i].request.headers;
                    result = histories[i].response.result;
                    //currentRequest.query_args = histories[i].request.query_args;
                    //currentRequest.form_args = histories[i].request.form_args;
                    //currentRequest.input_json = histories[i].request.input_json;
                    //currentRequest.url = histories[i].request.url;
                }
            }
            ImGui::EndChild();
            //ImGui::EndChild();

            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Collections"))
        {
            const CollectionNode* node = tree.getNodeById(selectedNodeId);
            const bool isNodeSelected = node != nullptr;
            const bool isRequest = isNodeSelected && node->requestIndex != CollectionTree::InvalidIndex;

            // TODO: remove once these buttons are implemented
            ImGui::BeginDisabled();

            if (isNodeSelected == false) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Discard changes")) {
                // TODO
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                // TODO
            }
            ImGui::SameLine();
            if (isNodeSelected == false) {
                ImGui::EndDisabled();
            }
            if (isNodeSelected == false || isRequest) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Add folder")) {
                // TODO
            }
            ImGui::SameLine();
            if (ImGui::Button("Add request")) {
                // TODO
            }
            if (isNodeSelected == false || isRequest) {
                ImGui::EndDisabled();
            }

            ImGui::EndDisabled();

            int dirtyFlagIndex;
            int selectedId = renderCollections(tree, selectedNodeId);

            ImGui::EndTabItem();
            if (selectedId != CollectionTree::InvalidId)
            {
                selectedNodeId = selectedId;
            }
        }
        ImGui::EndTabBar();
        ImGui::EndChild();

        ImGui::SameLine();

        {
            ImGui::BeginGroup();
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            ImGui::BeginChild("MainMenu", ImVec2(0, 0), false, window_flags);

            if (selectedNodeId != CollectionTree::InvalidId) {
                auto currentNode = tree.getNodeById(selectedNodeId);
                if (currentNode->requestIndex != CollectionTree::InvalidIndex) {
                    currentRequest = &tree.requests[currentNode->requestIndex];
                }
                else {
                    currentRequest = nullptr;
                }
            }

            if (currentRequest != nullptr) {

                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.125);
                if (ImGui::BeginCombo("##request_type", requestTypeStrings[(int)currentRequest->req_type])) {
                    for (int n = 0; n < (int)RequestType::_COUNT; n++) {
                        if (ImGui::Selectable(requestTypeStrings[n])) {
                            currentRequest->req_type = (RequestType)n;
                            currentRequest->body_type = BodyType::MULTIPART_FORMDATA;

                            tree.setDirty(selectedNodeId, true);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::InputText("##URL", currentRequest->url.buf_, currentRequest->url.capacity_, ImGuiInputTextFlags_EnterReturnsTrue) ) {
                    ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
                    tree.setDirty(selectedNodeId, true);
                    processRequest(thread, histories, *currentRequest, thread_status);
                }
                if (ImGui::IsItemEdited())
                {
                    currentRequest->query_args.clear();
                    deconstructUrl(currentRequest->url.buf_, currentRequest->query_args);
                    tree.setDirty(selectedNodeId, true);
                }

                if (thread_status == FINISHED) {
                    thread.join();
                    thread_status = IDLE;
                    selected = (int)histories.size()-1;
                    saveHistory(histories, "history.json", settings.PrettifyCollectionsJson);
                    update_hist_search = true;
                }
            }

            ImGui::BeginTabBar("requesttabs");
            if (ImGui::BeginTabItem("Params"))
            {
                static pg::Vector<int> delete_arg_btn;
                
                if (currentRequest != nullptr) {
                    bool argsDirty = false;
                    for (int i=0; i<(int)currentRequest->query_args.size(); i++) {
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.4);
                        char arg_name[32];
                        sprintf(arg_name, "Name##arg name%d", i);
                        if (ImGui::InputText(arg_name, &currentRequest->query_args[i].name[0], currentRequest->query_args[i].name.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                            tree.setDirty(selectedNodeId, true);
                            processRequest(thread, histories, *currentRequest, thread_status);
                        }
                        if (!argsDirty && ImGui::IsItemEdited()) argsDirty = true;
                        ImGui::SameLine();
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.6);
                        sprintf(arg_name, "Value##arg name%d", i);
                        if (ImGui::InputText(arg_name, &currentRequest->query_args[i].value[0], currentRequest->query_args[i].value.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                            tree.setDirty(selectedNodeId, true);
                            processRequest(thread, histories, *currentRequest, thread_status);
                        }
                        if (!argsDirty && ImGui::IsItemEdited()) argsDirty = true;
                        ImGui::SameLine();
                        char btn_name[32];
                        sprintf(btn_name, "Delete##arg delete%d", i);
                        if (ImGui::Button(btn_name)) {
                            if (curr_arg_file == i) {
                                curr_arg_file = -1;
                                picking_file = false;
                            }
                            delete_arg_btn.push_back(i);
                            argsDirty = true;
                            tree.setDirty(selectedNodeId, true);
                        }
                    }

                    if (currentRequest->query_args.empty())
                    {
                        ImGui::Text("No arguments to show");
                    }
                    
                    if (ImGui::Button("Add Argument")) {
                        Argument arg;
                        arg.arg_type = 0;
                        currentRequest->query_args.push_back(arg);
                        tree.setDirty(selectedNodeId, true);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete all args") && thread_status != RUNNING) {
                        currentRequest->query_args.clear();
                        argsDirty = true;
                        tree.setDirty(selectedNodeId, true);
                    }

                    // delete the args
                    if (thread_status != RUNNING) {
                        for (int i=(int)delete_arg_btn.size(); i>0; i--) {
                            currentRequest->query_args.erase(currentRequest->query_args.begin()+delete_arg_btn[i-1]);
                        }
                    }
                    delete_arg_btn.clear();

                    if (argsDirty)
                    {
                        currentRequest->url = buildUrl(currentRequest->url.buf_, currentRequest->query_args);
                        tree.setDirty(selectedNodeId, true);
                    }
                }
                else {
                    ImGui::Text("TODO: show folder-level parameters/variables");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Authorization"))
            {
                if (currentRequest != nullptr) {
                    const char* previewText = nullptr;
                    if ((int)currentRequest->auth.type >= 0 && (int)currentRequest->auth.type <= (int)AuthType::_LAST) {
                        previewText = authTypeUIStrings[(int)currentRequest->auth.type];
                    }

                    if (ImGui::BeginCombo("Type", previewText)) {
                        
                        for (int i = 0; i < (int)AuthType::_COUNT; i++) {
                            const bool selected = currentRequest->auth.type == (AuthType)i;
                            ImGui::Selectable(authTypeUIStrings[i], selected);
                        }

                        ImGui::EndCombo();
                    }

                    for (int i=0; i< (int)currentRequest->auth.attributes.size(); i++) {
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.5);
                        char arg_name[32];
                        sprintf(arg_name, "Name##auth arg name%d", i);
                        if (ImGui::InputText(arg_name, &currentRequest->auth.attributes[i].key[0], currentRequest->auth.attributes[i].key.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                            // TODO
                        }
                        ImGui::SameLine();
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.5);
                        sprintf(arg_name, "Value##auth arg value%d", i);
                        if (ImGui::InputText(arg_name, &currentRequest->auth.attributes[i].value[0], currentRequest->auth.attributes[i].value.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                            // TODO
                        }
                    }
                }

                ImGui::EndTabItem();
            }

            if (currentRequest != nullptr) {
                if (ImGui::BeginTabItem("Headers"))
                {
                    static pg::Vector<int> delete_arg_btn;
                    for (int i=0; i<(int)currentRequest->headers.headers.size(); i++) {
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.2);
                        char arg_name[32];
                        sprintf(arg_name, "Name##header arg name%d", i);
                        if (ImGui::InputText(arg_name, &currentRequest->headers.headers[i].key[0], currentRequest->headers.headers[i].key.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                            tree.setDirty(selectedNodeId, true);
                            processRequest(thread, histories, *currentRequest, thread_status);
                        }
                        ImGui::SameLine();
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.4);
                        sprintf(arg_name, "Value##header arg value%d", i);
                        if (ImGui::InputText(arg_name, &currentRequest->headers.headers[i].value[0], currentRequest->headers.headers[i].value.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                            tree.setDirty(selectedNodeId, true);
                            processRequest(thread, histories, *currentRequest, thread_status);
                        }
                        ImGui::SameLine();
                        char btn_name[32];
                        sprintf(btn_name, "Delete##header arg delete%d", i);
                        if (ImGui::Button(btn_name)) {
                            tree.setDirty(selectedNodeId, true);
                            delete_arg_btn.push_back(i);
                        }
                    }

                    if (currentRequest->headers.headers.empty())
                    {
                        ImGui::Text("No headers to show");
                    }
                    
                    // delete headers
                    for (int i=(int)delete_arg_btn.size(); i>0; i--) {
                        currentRequest->headers.headers.erase(currentRequest->headers.headers.begin()+delete_arg_btn[i-1]);
                    }
                    delete_arg_btn.clear();

                    if (ImGui::Button("Add Header Arg")) {
                        currentRequest->headers.headers.push_back(HeaderKeyValue{ .enabled = true });
                        tree.setDirty(selectedNodeId, true);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete all headers") && thread_status != RUNNING) {
                        currentRequest->headers.headers.clear();
                        tree.setDirty(selectedNodeId, true);
                    }

                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Body"))
                {
                    switch (currentRequest->req_type) {
                        case RequestType::GET:
                        case RequestType::DELETE:
                        case RequestType::OPTIONS: break;
                        case RequestType::POST:
                        case RequestType::PATCH:
                        case RequestType::PUT:
                            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.25);
                            if (ImGui::BeginCombo("##content_type", bodyTypeUIStrings[(int)currentRequest->body_type])) {
                                for (int n = 0; n < (int)BodyType::_COUNT; n++) {
                                    if (ImGui::Selectable(bodyTypeUIStrings[n])) {
                                        currentRequest->body_type = (BodyType)n;
                                        tree.setDirty(selectedNodeId, true);
                                    }
                                }
                                ImGui::EndCombo();
                            }
                            break;
                        case RequestType::_COUNT: break; // make the compiler happy
                    }
                    if (currentRequest->body_type == BodyType::RAW)
                    {
                        ImGui::SameLine();
                        
                        const char* contentType = currentRequest->headers.findHeaderValue("Content-Type");
                        int index = HeaderKeyValueCollection::contentTypeToRawBodyTypeIndex(contentType);
                        const char* rawBodyType = index >= 0 ? rawBodyTypeUIStrings[index] : nullptr;

                        if (ImGui::BeginCombo("##raw_body_type", rawBodyType))
                        {
                            for (int i = 0; i < (int)RawBodyType::_COUNT; i++)
                            {
                                if (ImGui::Selectable(rawBodyTypeUIStrings[i])) {
                                    currentRequest->headers.setHeaderValue("Content-Type", rawBodyTypeStrings[i]);
                                    tree.setDirty(selectedNodeId, true);
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }

                    if ((currentRequest->req_type == RequestType::POST || currentRequest->req_type == RequestType::PUT || currentRequest->req_type == RequestType::PATCH))
                    {
                        if (currentRequest->body_type == BodyType::RAW) {

                            const char* contentType = currentRequest->headers.findHeaderValue("Content-Type");
                            bool isJson = contentType != nullptr && strcmp(contentType, "application/json") == 0;
                            bool isXml = contentType != nullptr && strcmp(contentType, "application/xml") == 0;
                            const char* label = isJson ? "Input JSON"
                                : isXml ? "Input XML"
                                : "Input";

                            ImGui::TextUnformatted(label);
                            if (isJson)
                            {
                                rapidjson::Document d;
                                // TODO: only check for JSON errors on changes instead of every frame
                                if (d.Parse(currentRequest->input_json.buf_).HasParseError() && currentRequest->input_json.length() > 0) {
                                    ImGui::SameLine();
                                    ImGui::Text("Problems with JSON");
                                }
                            }
                            int block_height = ImGui::GetContentRegionAvail()[1];
                            block_height /= 2;
                            if (ImGui::InputTextMultiline("##input_json", &currentRequest->input_json[0], currentRequest->input_json.capacity(), ImVec2(-1.0f, block_height), ImGuiInputTextFlags_AllowTabInput)) {
                                tree.setDirty(selectedNodeId, true);
                            }
                        }
                        else {
                            static pg::Vector<int> delete_arg_btn;

                            for (int i=0; i<(int)currentRequest->form_args.size(); i++) {
                                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.2);
                                char combo_name[32];
                                sprintf(combo_name, "##combo arg type%d", i);
                                ImGui::Combo(combo_name, &currentRequest->form_args[i].arg_type, arg_types[(int)currentRequest->req_type], num_arg_types[(int)currentRequest->req_type]);
                                ImGui::SameLine();
                                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.2);
                                char arg_name[32];
                                sprintf(arg_name, "Name##arg name%d", i);
                                if (ImGui::InputText(arg_name, &currentRequest->form_args[i].name[0], currentRequest->form_args[i].name.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                                    tree.setDirty(selectedNodeId, true);
                                    processRequest(thread, histories, *currentRequest, thread_status);
                                }
                                ImGui::SameLine();
                                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x*0.6);
                                sprintf(arg_name, "Value##arg name%d", i);
                                if (ImGui::InputText(arg_name, &currentRequest->form_args[i].value[0], currentRequest->form_args[i].value.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                                    tree.setDirty(selectedNodeId, true);
                                    processRequest(thread, histories, *currentRequest, thread_status);
                                }
                                ImGui::SameLine();
                                if (currentRequest->form_args[i].arg_type == 1) {
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
                                    tree.setDirty(selectedNodeId, true);
                                    if (curr_arg_file == i) {
                                        curr_arg_file = -1;
                                        picking_file = false;
                                    }
                                    delete_arg_btn.push_back(i);
                                }
                            }

                            if (currentRequest->form_args.empty())
                            {
                                ImGui::Text("No arguments to show");
                            }
                            
                            if (ImGui::Button("Add Argument")) {
                                Argument arg;
                                arg.arg_type = 0;
                                currentRequest->form_args.push_back(arg);
                                tree.setDirty(selectedNodeId, true);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Delete all args") && thread_status != RUNNING) {
                                currentRequest->form_args.clear();
                                tree.setDirty(selectedNodeId, true);
                            }

                            // delete the args
                            if (thread_status != RUNNING) {
                                for (int i=(int)delete_arg_btn.size(); i>0; i--) {
                                    currentRequest->form_args.erase(currentRequest->form_args.begin()+delete_arg_btn[i-1]);
                                }
                            }
                            delete_arg_btn.clear();
                        }
                    }

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();

            if (currentRequest != nullptr) {
                ImGui::Text("Result");
                ImGui::BeginTabBar("resulttabs");
                if (ImGui::BeginTabItem("Body"))
                {
                    if (histories.size() > 0) {
                        if (selected >= histories.size()) {
                            selected = (int)histories.size()-1;
                        }
                        ImGui::InputTextMultiline("##source", &histories[selected].response.result[0], histories[selected].response.result.capacity(), ImVec2(-1.0f, ImGui::GetContentRegionAvail()[1]), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_ReadOnly);
                    }
                    else {
                        char blank[] = "";
                        ImGui::InputTextMultiline("##source", blank, 0, ImVec2(-1.0f, ImGui::GetContentRegionAvail()[1]), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_ReadOnly);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Headers"))
                {
                    if (histories.size() > 0) {
                        if (selected >= histories.size()) {
                            selected = (int)histories.size()-1;
                        }

                        if (ImGui::BeginTable("response_headers", 2)) {

                            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                            ImGui::TableNextColumn();
                            ImGui::Text("Name");
                            ImGui::TableNextColumn();
                            ImGui::Text("Value");

                            int i = 0;
                            for (auto itr = histories[selected].response.result_headers.headers.begin();
                                itr != histories[selected].response.result_headers.headers.end();
                                ++itr)
                            {
                                ImGui::PushID(i);
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::InputText("##name", (*itr).key.buf_, (*itr).key.capacity(), ImGuiInputTextFlags_ReadOnly);
                                ImGui::TableNextColumn();
                                ImGui::InputText("##value", (*itr).value.buf_, (*itr).value.capacity(), ImGuiInputTextFlags_ReadOnly);
                                ImGui::PopID();
                            
                                i++;
                            }

                            ImGui::EndTable();
                        }
                    }
                    else {

                    }

                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::EndChild();
            ImGui::EndGroup();
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
#ifdef _WIN32
                // TODO: verify that this actually works!
                curr_dir = aux_dir;
#else
                realpath(aux_dir.buf_, curr_dir.buf_);
#endif
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
                    if (curr_arg_file >= 0 && curr_arg_file < currentRequest->form_args.size()) {
                        pg::String filename = curr_dir;
                        filename.append("/");
                        filename.append(curr_files[i]);
                        currentRequest->form_args[curr_arg_file].value = filename;
                    }                    

                    picking_file = false;
                    curr_arg_file = -1;
                }
            }
            ImGui::End();
        }

        ImGui::ShowDemoWindow();

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
        end = Platform::getUtcTimestampNow();
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

