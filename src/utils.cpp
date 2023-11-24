#include "utils.h"
#include "platform.h"
#include "history.h"

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

void printHeader(const HeaderKeyValue& header) {
    printf("name: %s\nvalue: %s\nenabled: %s\n\n", header.key.buf_, header.value.buf_, header.enabled ? "true" : "false");
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


