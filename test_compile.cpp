#include <string>
#include <iostream>
int main() {
    try {
        std::string line = "user_preview.hlsl:20:5: error: expected '('";
        size_t firstColon = line.find(':');
        size_t secondColon = line.find(':', firstColon + 1);
        size_t thirdColon = line.find(':', secondColon + 1);
        std::cout << firstColon << " "" << secondColon << " "" << thirdColon << " \n";
        const std::string lineStr = line.substr(firstColon + 1, secondColon - firstColon - 1);
        const std::string colStr = line.substr(secondColon + 1, thirdColon - secondColon - 1);
        std::cout << "lineStr: "" << lineStr << " colStr: "" << colStr << "\n";
    } catch(std::exception& e) {
        std::cout << e.what() << "\n";
    }
    return 0;
}
