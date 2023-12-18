#pragma once

#include "imgui.h"

const char* renderComboBox(const char* label, const char* currentValue, int numValue, const char* values[], const char* texts[]) {
    

    // find the selected value
    int selectedIndex = 0;
    for (int i = 0; i < numValue; i++) {
        if (currentValue != nullptr && strcmp(values[i], currentValue) == 0) {
            selectedIndex = i;
            break;
        }
    }

    if (ImGui::Combo(label, &selectedIndex, texts, numValue)) {
        // nothing
    }

    if (numValue == 0) {
        return nullptr;
    }
    return values[selectedIndex];
};