
import re
import json
import subprocess
import sys

KERNEL = "kernel/kernel"
OUTPUT = "kernel/backtrace_table.h"

# 1. Lấy danh sách địa chỉ instruction từ objdump.
result = subprocess.run(
    ["riscv64-unknown-elf-objdump", "-d", KERNEL],
    capture_output=True,
    text=True,
    check=True
)

addresses = []

for line in result.stdout.splitlines():
    match = re.match(r"^\s*([0-9a-f]+):", line)
    if match:
        addresses.append(int(match.group(1), 16))

addresses = sorted(set(addresses))

if not addresses:
    sys.exit("Khong tim thay instruction!")

# 2. Dùng addr2line tra cứu hàng loạt địa chỉ.
args = [
    "riscv64-unknown-elf-addr2line",
    "-a", "-f", "-e", KERNEL
] + [hex(addr) for addr in addresses]

result = subprocess.run(
    args,
    capture_output=True,
    text=True,
    check=True
)

lines = result.stdout.splitlines()

# Mỗi địa chỉ có 3 dòng: địa chỉ, tên hàm, file:dòng.
records = []

for i in range(0, len(lines), 3):
    if i + 2 >= len(lines):
        break

    try:
        addr = int(lines[i], 16)
    except ValueError:
        continue

    function = lines[i + 1]
    location = lines[i + 2]

    match = re.match(r"(.+):(\d+)(?:\s+\(.*\))?$", location)

    if not match or function == "??":
        continue

    filename = match.group(1).removeprefix("./")
    lineno = int(match.group(2))

    records.append((addr, function, filename, lineno))

# 3. Gộp các instruction liên tiếp có cùng thông tin.
ranges = []

for addr, function, filename, lineno in records:
    if ranges:
        last = ranges[-1]

        if (
            last[3] == function
            and last[4] == filename
            and last[5] == lineno
        ):
            last[1] = addr
            continue

    ranges.append([addr, addr, function, filename, lineno])

# 4. Xuất bảng C.
with open(OUTPUT, "w") as f:
    f.write("#ifndef BACKTRACE_TABLE_H\n")
    f.write("#define BACKTRACE_TABLE_H\n\n")

    f.write("struct backtrace_entry {\n")
    f.write("  unsigned long start;\n")
    f.write("  unsigned long end;\n")
    f.write("  const char *function;\n")
    f.write("  const char *file;\n")
    f.write("  int line;\n")
    f.write("};\n\n")

    f.write("static const struct backtrace_entry backtrace_table[] = {\n")

    for start, end, function, filename, lineno in ranges:
        f.write(
            "  {%s, %s, %s, %s, %d},\n" % (
                hex(start),
                hex(end),
                json.dumps(function),
                json.dumps(filename),
                lineno
            )
        )

    f.write("};\n\n")
    f.write("#define BACKTRACE_TABLE_SIZE \\\n")
    f.write("  (sizeof(backtrace_table) / sizeof(backtrace_table[0]))\n\n")
    f.write("#endif\n")

print("Da tao:", OUTPUT)
print("So instruction:", len(addresses))
print("So vung dia chi:", len(ranges))
