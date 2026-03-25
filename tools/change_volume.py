offset = 0x10  # Change as needed
inside_array = False
output_lines = []

with open("audio_example_file.h") as f:
    for line in f:
        if "audio_table" in line and "{" in line:
            inside_array = True
            output_lines.append(line)
            continue
        if inside_array:
            if "};" in line:
                inside_array = False
                output_lines.append(line)
                continue
            # Process hex values in this line
            values = [v.strip() for v in line.split(",") if v.strip()]
            new_values = []
            for v in values:
                if v.startswith("0x"):
                    num = max(0, int(v, 16) - offset)
                    new_values.append(f"0x{num:02X}")
                else:
                    new_values.append(v)
            output_lines.append(", ".join(new_values) + ("," if line.strip().endswith(",") else "") + "\n")
        else:
            output_lines.append(line)

with open("audio_example_file_low.h", "w") as f:
    f.writelines(output_lines)