// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS      10
#define HISTORY_SIZE 10  // số lệnh tối đa được lưu.
#define CMD_SIZE     100 // kích thước bộ nhớ dành cho mỗi lệnh.

static char
  cmd_history[HISTORY_SIZE]
             [CMD_SIZE];      // mảng hai chiều dùng để lưu nội dung các lệnh.
static int history_count = 0; // Tổng số lệnh được lưu
static int history_next =
  0; // chỉ số của ô tiếp theo sẽ được sử dụng để ghi lệnh mới vào cmd_history

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

int
prefix_match(char *prefix, char *name)
{
  // Hàm này kiểm tra xem name có bắt đầu bằng prefix không.
  while (*prefix) {
    if (*prefix != *name)
      return 0;
    prefix++;
    name++;
  }
  return 1;
}

int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v';
}

int
is_program(char *name)
{
  // Hàm này kiểm tra một chương trình xv6 có định dạng ELF hay không
  char
    path[DIRSIZ + 2]; // lưu đường dẫn tuyệt đối của chương trình cần kiểm tra
  char magic[4];      // lưu 4 byte đầu tiên đọc từ chương trình
  path[0] = '/';      // đặt dấu / ở đầu đường dẫn
  memmove(path + 1, name, DIRSIZ); // sao chép tên vào phía sau dấu /
  path[DIRSIZ + 1] = '\0';         // đặt ký tự kết thúc chuỗi tại cuối mảng

  int fd = open(path, 0); // Mở đường dẫn
  if (fd < 0)             // Không mở được
    return 0;
  // Đọc 4 byte đầu tiên
  int n = read(fd, magic, sizeof(magic));
  close(fd);
  return n == 4 && (unsigned char)magic[0] == 0x7f && magic[1] == 'E' &&
         magic[2] == 'L' && magic[3] == 'F';
}

int
tab_match(char *name, char *prefix, int first)
{
  struct stat st;
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) // Bỏ qua . và ..
    return 0;
  if (!prefix_match(prefix, name)) // Kiểm tra name có tiền tố prfix không
    return 0;
  if (first)
    return is_program(
      name); // Nếu first khác 0 thì kiểm tra name có phải file thực thi ELF không
  // Nếu first = 0 thì kiểm tra mục có phải là file thông thường không
  int fd = open(name, 0); // Mở mục theo tên
  if (fd < 0)             // Mở không thành công
    return 0;
  int ok = fstat(fd, &st) >= 0 && st.type == T_FILE;
  close(fd);
  return ok;
}

static int
history_slot(int pos)
{
  // trả về chỉ số thực tế trong mảng cmd_history của một lệnh trong lịch sử, bởi vì history được lưu vòng
  return (history_next - history_count + pos + HISTORY_SIZE) % HISTORY_SIZE;
}

static void
copy_text(char *dst, const char *src, int cap)
{
  int n = strlen(src);
  if (n >= cap)
    n = cap - 1;
  memmove(dst, src, n);
  dst[n] = '\0';
}

static void
history_add(const char *cmd)
{
  // Lưu 1 lệnh vào history log
  int n = strlen(cmd);
  // Nếu lệnh rỗng thì bỏ qua
  if (n == 0)
    return;
  // Nếu chuỗi dài quá quy định thì rút ngắn lại
  if (n >= CMD_SIZE)
    n = CMD_SIZE - 1;
  memmove(cmd_history[history_next], cmd,
          n); // cmd_history[history_next] = cmd với độ dài là n
  cmd_history[history_next][n] = '\0';
  // cập nhật lại các thông số của history log
  history_next = (history_next + 1) % HISTORY_SIZE;
  if (history_count < HISTORY_SIZE)
    history_count++;
}

static int
history_get(int number, char *dst, int cap)
{
  // number: index muốn lấy trong history log
  // dst: để lưu kết quả
  // cap là size tối đa của lệnh trong lịch sử muốn lấy
  // Hàm này để lấy dòng lệnh thứ number trong history log
  if (number < 1 || number > history_count)
    return 0;
  int slot = history_slot(number - 1);
  copy_text(dst, cmd_history[slot], cap);
  return 1;
}

static void
history_print(void)
{
  // In history log
  int i;
  for (i = 0; i < history_count; i++) {
    int slot = history_slot(i);
    fprintf(1, "%d %s\n", i + 1, cmd_history[slot]);
  }
}

static int
history_expand(char *cmd, char *out, int cap)
{
  // Hàm nhận diện cú pháp gọi lại lệnh trong lịch sử, chẳng hạn !! hoặc !n
  // cmd là chuỗi lệnh người dùng nhập
  // out là bộ đệm lưu nội dung lệnh được lấy từ lịch sử
  // cap là sức chứa tối đa của bộ đệm out
  int number = 0; // lưu số thứ tự lệnh cần lấy từ lịch sử
  int i;
  if (strcmp(cmd, "!!") == 0)
    number = history_count; // Nếu !! là lấy lệnh mới nhất
  else if (cmd[0] == '!' && cmd[1] >= '0' && cmd[1] <= '9') {
    for (i = 1; cmd[i] != '\0'; i++) {
      if (cmd[i] < '0' || cmd[i] > '9')
        return 0;
      number = number * 10 + (cmd[i] - '0');
      if (number > 100000)
        return -1;
    }
  } else
    return 0;
  if (!history_get(number, out, cap)) {
    fprintf(2, "history: event not found\n");
    return -1;
  }
  fprintf(1, "%s\n", out);
  return 1;
}

void
tab_complete(char *buf, int *len, int nbuf)
{
  // buf là bộ đệm chứa dòng lệnh đang nhập
  // len là con trỏ đến độ dài hiện tại của dòng lệnh (có thể thay đổi).
  // nbuf là kích thược tối đa của bộ đệm buf.
  int i;
  int start = *len; // độ dài hiện tại của dòng lệnh
  while (start > 0 &&
         !is_space(
           buf[start -
               1])) // Lùi dần cho đến khi gặp khoảng trắng hoặc chạm đầu dòng
    start--;

  int first =
    1; // Xác định từ hiện tại có phải là từ đầu tiên của dòng lệnh không
  // first = 1 là có (ec)
  // first = 0 là không (cat REA)
  for (i = 0; i < start; i++) {
    if (!is_space(buf[i])) {
      first = 0;
      break;
    }
  }

  int plen = *len - start; // Số ký tự của từ đang nhập
  if (plen > DIRSIZ)
    return;
  // Tạo biến tiền tố
  char prefix[DIRSIZ + 1];
  memmove(prefix, buf + start,
          plen); // sao chép plen byte từ buf + start sang prefix.
  prefix[plen] = '\0';

  // Nếu first = 0 mở thư mục .; = 1 thì mở thư mục /
  // Mở . để tìm tên file trong thư mục hiện tại
  // Mở / để tìm chương trình ở thư mục gốc
  int fd = open(first ? "/" : ".", 0);
  if (fd < 0)
    return;

  struct dirent de; // sử dụng file descriptor để đọc từng mục trong thư mục
  char
    only_match[DIRSIZ +
               1]; // lưu tên đầu tiên hợp với tiền tố để sử dụng nếu có kết quả
  int nmatch = 0;  // đếm số tên phù hợp với tiền tố
  // Vòng lặp đọc lần lượt từng mục
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0)
      continue;
    char name[DIRSIZ + 1];
    memmove(name, de.name, DIRSIZ); // Lấy tên mục
    name[DIRSIZ] = '\0';
    // Kiểm tra name có khớp tiền tố và có phù hợp với loại mục cần hoàn thành hay không
    if (!tab_match(name, prefix, first))
      continue;
    nmatch++;
    if (nmatch == 1) // Nếu là tên đầu tiên match được thì lưu vào only_match
      memmove(only_match, name, DIRSIZ + 1);
  }
  close(fd);

  if (nmatch == 1) // Nếu chỉ có duy nhất 1 mục thỏa điều kiện
  {
    int namelen = strlen(only_match);
    int add = namelen - plen;
    if (*len + add >= nbuf)
      add = nbuf - *len - 1; // Giới hạn ký tự
    if (add > 0) {
      memmove(
        buf + *len, only_match + plen,
        add); // Sao chép phần còn thiếu vào cuối dòng lệnh đang lưu trong buf
      write(
        1, only_match + plen,
        add); // In phần vừa bổ sung lên màn hình để người dùng thấy kết quả hoàn thành
      *len += add;
      buf[*len] = '\0';
    }
    return;
  }
  if (nmatch > 1) {
    // Nếu có nhiều tên phù hợp, chương trình mở lại thư mục vì lần duyệt đầu tiên chỉ đếm và lưu kết quả đầu tiên.
    // Lần thứ hai sẽ in toàn bộ các tên phù hợp.
    fd = open(first ? "/" : ".", 0);
    if (fd < 0)
      return;
    write(1, "\n", 1); // In dấu xuống dòng
    int printed = 0;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      // Đọc từng mục và kiểm tra match? nếu match thì in
      if (de.inum == 0)
        continue;
      char name[DIRSIZ + 1];
      memmove(name, de.name, DIRSIZ);
      name[DIRSIZ] = '\0';
      if (!tab_match(name, prefix, first))
        continue;
      if (printed > 0)
        write(1, "  ", 2);
      write(1, name, strlen(name));
      printed++;
    }
    close(fd);
    // Xuống dòng, in $ và in lại phần lại phần dòng lệnh mà người dòng đã gõ để người dùng có thể tiếp tục nhập
    write(1, "\n", 1);
    write(2, "$ ", 2);
    write(2, buf, *len);
  }
}

static void
replace_input_line(char *buf, int *len, int nbuf, const char *newtext)
{
  // Hàm dùng để xóa dòng lệnh đang hiển thị, thay nội dung trong buf bằng một chuỗi mới, rồi hiển thị chuỗi mới lên màn hình
  // buf là bộ đệm lưu dòng lệnh hiện tại
  // len là con trỏ đến độ dài hiện tại của dòng lệnh
  // nbuf	là kích thước tối đa của bộ đệm buf.
  // newtext là chuỗi mới muốn đưa vào dòng lệnh, thường là lệnh lấy từ lịch sử.
  while (*len > 0) {
    // lùi con trỏ màn hình một vị trí, ghi đè ký tự đang hiển thị bằng khoảng trắng, lùi con trỏ lại vị trí vừa xóa
    // Vòng lặp chỉ xóa ở màn hình hiển thị
    write(1, "\b \b", 3);
    (*len)--;
  }
  int n = strlen(newtext);
  if (n >= nbuf)
    n = nbuf - 1;
  memmove(buf, newtext, n); // buf = newtext
  buf[n] = '\0';
  *len = n;
  write(1, buf, n); // in buf - dòng lệnh mới
}

int
getcmd(char *buf, int nbuf)
{
  struct stat st;
  int interactive = fstat(0, &st) >= 0 && st.type == T_DEVICE;
  if (interactive) {
    // Nếu đầu vào chuẩn là lệnh in prompt thì in $
    write(2, "$ ", 2);
  }

  // gàn giá trị 0 cho số nbuf bytes cho vùng nhớ buf
  memset(buf, 0, nbuf);
  int len = 0; // Số ký tự hiện có trong dòng lệnh, không tính ký tự '\0'
  int ended_line =
    0; // Đánh dấu đã đọc ký tự xuống dòng '\n' hoặc '\r' hay chưa
  int edit_mode =
    0; // Đánh dấu shell đã chuyển sang chế độ tự xử lý việc hiển thị ký tự, chẳng hạn sau khi dùng Tab.
  int hist_pos =
    history_count; // Vị trí đang xem trong lịch sử lệnh. Ban đầu bằng history_count, tức vị trí sau lệnh mới nhất.
  int esc_state =
    0; // Theo dõi trạng thái khi đọc chuỗi phím mũi tên, thường có dạng ESC [ A hoặc ESC [ B.
  char draft
    [CMD_SIZE]; // Lưu tạm dòng lệnh đang nhập dở để khôi phục khi người dùng duyệt lên/xuống trong lịch sử.
  draft[0] = '\0'; // Chuỗi lúc đầu rỗng

  // Vòng lặp đọc đầu vào
  while (len < nbuf - 1) {
    char c;
    int n = read(0, &c, 1); // đọc từng byte từ đầu vào chuẩn vào biến c
    if (n <= 0)             // đọc không thành công
      break;

    if (c == '\n' || c == '\r') {
      // Đặt cờ đánh dấu kết dòng lệnh đã kết thúc và thoát vòng lặp
      ended_line = 1;
      break;
    }

    if (interactive) // Xử lý phím mũi tên
    {
      // Phím mũi tên lên thường tạo chuỗi ESC [ A và mũi tên xuống là ESC [ B
      // ESC có mã '\x1b', [ là byte tiếp theo, A(B) là biểu thị mũi tên lên(xuống)
      if (esc_state == 1) // đã đọc được mã 'x1b' trước đó
      {
        if (c == '[') {
          // Tiếp tục đọc ký tự tiếp theo phải A hoặc B không
          esc_state = 2;
          continue;
        }
        esc_state = 0;           // Nếu không phải [ thì đặt cờ lại về 0 (reset)
      } else if (esc_state == 2) // đã đọc được mã 'x1b' và '[' trước đó
      {
        esc_state = 0; // reset cờ
        if (c == 'A' || c == 'B') {
          edit_mode = 1;
          if (c == 'A' &&
              history_count >
                0) // Kiểm tra xem history có lệnh không với phím mũi tên lên
          {
            if (hist_pos == history_count) {
              // Nếu đang ở vị trí cuối lịch sử, lưu dòng hiện tại đang nhập dở vào draft.
              copy_text(draft, buf, sizeof(draft));
            }

            if (hist_pos > 0)
              hist_pos--; // Giảm hist_pos để chọn lệnh cũ hơn

            char line[CMD_SIZE]; // CMD_SIZE = 100
            if (history_get(
                  hist_pos + 1, line,
                  sizeof(line))) // Lấy lệnh từ lịch sử bằng history_get()
              replace_input_line(
                buf, &len, nbuf,
                line); // Thay thế dòng đang nhập bằng lệnh cũ nhờ replace_input_line()
          }
          // Xử lý tương tự với phím mũi tên xuống
          else if (
            c == 'B' &&
            hist_pos <
              history_count) // Vị trí hiện tại phải nhỏ hơn tổng độ dài history
          {
            hist_pos++;
            // Chương trình di chuyển đến lệnh mới hơn. Nếu đã quay lại vị trí sau lệnh cuối cùng, nó khôi phục dòng đang nhập dở từ draft.
            if (hist_pos == history_count) {
              // Khôi phục dòng đang nhập dở (draft)
              replace_input_line(buf, &len, nbuf, draft);
            } else {
              char line[CMD_SIZE];
              // Khôi phục lệnh trong history (line)
              if (history_get(hist_pos + 1, line, sizeof(line)))
                replace_input_line(buf, &len, nbuf, line);
            }
          }
          continue;
        }
      } else if (c == '\x1b') // Đặt cờ để đọc ký tự '[' tiếp theo
      {
        esc_state = 1;
        continue;
      }
    }

    // Bài 3b: Tab completion.
    if (interactive && c == '\t') {
      edit_mode = 1;                 // Đặt cờ
      tab_complete(buf, &len, nbuf); // Gọi hàm tab_complete() để xử lý
      continue;
    }

    // Backspace: xóa một ký tự cuối.
    if (interactive && edit_mode && (c == '\b' || c == '\x7f')) {
      if (len > 0) {
        len--;
        buf[len] = '\0';
        write(1, "\b \b", 3);
      }
      continue;
    }

    // Ctrl-U: xóa toàn bộ dòng đang nhập.
    if (interactive && edit_mode && c == '\x15') {
      while (len > 0) {
        len--;
        write(1, "\b \b", 3);
      }
      buf[0] = '\0';
      hist_pos = history_count;
      draft[0] = '\0';
      continue;
    }

    // Ký tự thông thường
    // Trong edit_mode, shell tự echo vì kernel không echo ký tự
    if (edit_mode)
      write(
        1, &c,
        1); // hiển thị ký tự c trên màn hình nếu shell đang ở chế độ tự echo.

    // Cập nhật ký tự vào cuối dòng và tăng len
    buf[len++] = c;
    buf[len] = '\0';

    // Người dùng bắt đầu chỉnh sửa dòng hiện tại.
    hist_pos = history_count;
  }

  // Hết đầu vào và không có lệnh nào để xử lý.
  if (len == 0 && !ended_line)
    return -1;

  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    char *cmd = buf;
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\0')
      continue;
    char expanded[CMD_SIZE];
    int result = history_expand(cmd, expanded, sizeof(expanded));
    if (result < 0)
      continue;
    if (result > 0) {
      copy_text(buf, expanded, sizeof(buf));
      cmd = buf;
    }
    history_add(cmd);
    if (strcmp(cmd, "history") == 0) {
      history_print();
      continue;
    }
    if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
      // Chdir must be called by the parent, not the child.
      if (chdir(cmd + 3) < 0)
        fprintf(2, "cannot cd %s\n", cmd + 3);
    } else {
      if (fork1() == 0)
        runcmd(parsecmd(cmd));
      wait(0);
    }
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
