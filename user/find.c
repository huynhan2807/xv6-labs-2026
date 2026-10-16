#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

char *basename(char *path);
void runexec(char *path, char **cmd, int cmdargc);
void find(char *path, char *target, char **cmd, int cmdargc);

int main(int argc, char *argv[])
{
    if (argc == 3)
    {
        // Xử lý trường hợp find không có exec
        find(argv[1], argv[2], 0, 0);
    }
    else if (argc >= 5 && strcmp(argv[3], "-exec") == 0)
    {
        // Xử lý trường hợp find có exec
        char **cmd = &argv[4];
        int cmdargc = argc - 4;
        find(argv[1], argv[2], cmd, cmdargc);
    }
    else
    {
        fprintf(2, "Usage: find path target [-exec cmd [args...]]\n");
        exit(1);
    }
    exit(0);
}

// Lấy tên cuối cùng của đường dẫn
char *basename(char *path)
{
    char *name = path;
    for (char *p = path; *p != '\0'; p++)
    {
        if (*p == '/')
            name = p + 1;
    }
    return name;
}

// Chạy lệnh trên từng kết quả tìm được
void runexec(char *path, char **cmd, int cmdargc)
{
    // path là đường dẫn của mục vừa tìm thấy
    // cmd là mảng chứa tên lệnh và đối số
    // cmdargc là số phần tử trong mảng cmd
    int pid = fork(); // fork() tạo một tiến trình con từ tiến trình hiện tại
    if (pid < 0)
    {
        // Tạo tiến trình thất bại
        fprintf(2, "find: fork failed\n");
        return;
    }
    if (pid == 0)
    {
        char *args[MAXARG];
        if (cmdargc + 2 > MAXARG)
        {
            fprintf(2, "find: too many arguments\n");
            exit(1);
        }
        for (int i = 0; i < cmdargc; i++)
            args[i] = cmd[i];
        args[cmdargc] = path;
        args[cmdargc + 1] = 0;
        exec(args[0], args); // Thực thi lệnh bằng exec()
        fprintf(2, "find: exec %s failed\n", args[0]);
        exit(1);
    }
    wait(0); // Tiến trình wait() để đợi tiền trình con kết thúc
}

// Duyệt cây thư mục và tìm kiếm đệ quy
void find(char *path, char *target, char **cmd, int cmdargc)
{
    char buf[512]; // Bộ đệm dùng để tạo đường dẫn đầy đủ của mục con
    char *p; // Con trỏ đến vị trí ghi tên mục con trong buf
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, O_RDONLY)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if (strcmp(basename(path), target) == 0)
    {
        if (cmdargc == 0) // Không -exec
            printf("%s\n", path);
        else // Có -exec
            runexec(path, cmd, cmdargc);
    }

    if (st.type == T_DIR)
    {
        // len(path + "/"" + "tên mục con" + "\0") <= len(buf)
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            fprintf(2, "find: path too long\n");
            close(fd);
            return;
        }

        strcpy(buf, path);
        p = buf + strlen(buf); // p trỏ đến vị trí '\0' của buf
        if (p == buf || p[-1] != '/')
        {
            // Thêm '/' vào cuối mảng p.
            // Ví dụ: buf = 'abc' thì p = 'abc/', p vẫn trỏ đến '\0'
            *p = '/';
            p++;
        }

        // Vòng lặp đọc từng mục trong thư mục
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            // đọc các mục con trong thư mục path vào name
            // sau dùng memmove() để p = p + name tạo đường dẫn mới rồi gọi đệ quy
            char name[DIRSIZ + 1];
            if (de.inum == 0)
                continue;
            memmove(name, de.name, DIRSIZ);
            name[DIRSIZ] = '\0';

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            memmove(p, name, strlen(name) + 1);
            find(buf, target, cmd, cmdargc);
        }
    }
    close(fd);
}
