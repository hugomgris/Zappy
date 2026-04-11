import re

with open("/home/jareste/hugo/zappy/client_cpp/srcs/app/WorldState.cpp", "r") as f:
    text = f.read()

# Fix pose ko
pose_pattern = r'else if \(msg.isKo\(\)\) \{\s*Logger::error\("  PREND FAILED! Erasing from vision."\);\s*if \(\!_vision.empty\(\)\) \{\s*auto it = std::find\(_vision\[0\]\.items\.begin\(\), _vision\[0\]\.items\.end\(\), msg\.arg\);\s*if \(it != _vision\[0\]\.items\.end\(\)\) _vision\[0\]\.items\.erase\(it\);\s*\}\s*\}'
text = re.sub(pose_pattern, 'else if (msg.isKo()) {\n\t\t\t\t\t\tLogger::error("  POSE FAILED!");\n\t\t\t\t\t}', text)

# Fix incantation ko
incant_pattern = r'else if \(msg.isKo\(\)\) \{\s*Logger::error\("  PREND FAILED! Erasing from vision."\);\s*if \(\!_vision.empty\(\)\) \{\s*auto it = std::find\(_vision\[0\]\.items\.begin\(\), _vision\[0\]\.items\.end\(\), msg\.arg\);\s*if \(it != _vision\[0\]\.items\.end\(\)\) _vision\[0\]\.items\.erase\(it\);\s*\}\s*Logger::info\("  Incantation failed"\);\s*\}'
text = re.sub(incant_pattern, 'else if (msg.isKo()) {\n\t\t\t\t\t\tLogger::info("  Incantation failed");\n\t\t\t\t\t}', text)

with open("/home/jareste/hugo/zappy/client_cpp/srcs/app/WorldState.cpp", "w") as f:
    f.write(text)
