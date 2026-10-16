#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data, int len);

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data, int len)
{
  if (fmt == 0 || data == 0 || len < 0)
  {
    fprintf(2, "memdump: invalid arguments\n");
    return;
  }

  // memmove(destination, source, n); dest, source là địa chỉ vùng nhớ đích và nguồn
  // n là số byte cần sao chép
  uint total = (uint)len;
  for (int i = 0; fmt[i] != '\0'; i++)
  {
    if (fmt[i] == 'i')
    {
      int value;
      if (total < sizeof(value))
      {
          fprintf(2, "memdump: not enough data for 'i'\n");
          return;
      }
      memmove(&value, data, sizeof(value));
      printf("%d\n", value);
      data += sizeof(value);
      total -= sizeof(value);
    }
    else if (fmt[i] == 'p')
    {
      uint64 value;
      if (total < sizeof(value))
      {
        fprintf(2, "memdump: not enough data for 'p'\n");
        return;
      }
      memmove(&value, data, sizeof(value));
      printf("%p\n", value);
      data += sizeof(value);
      total -= sizeof(value);
    }
    else if (fmt[i] == 'h')
    {
      short value;
      if (total < sizeof(value))
      {
        fprintf(2, "memdump: not enough data for 'h'\n");
        return;
      }
      memmove(&value, data, sizeof(value));
      printf("%d\n", (int)value);
      data += sizeof(value);
      total -= sizeof(value);
    }
    else if (fmt[i] == 'c')
    {
      unsigned char value;
      if (total < sizeof(value))
      {
        fprintf(2, "memdump: not enough data for 'c'\n");
        return;
      }
      value = (unsigned char)data[0];
      printf("%c\n", value);
      data += sizeof(value);
      total -= sizeof(value);
    }
    else if (fmt[i] == 's')
    {
      char *str;
      if (total < sizeof(str))
      {
        fprintf(2, "memdump: not enough data for 's'\n");
        return;
      }
      memmove(&str, data, sizeof(str));
      data += sizeof(str);
      total -= sizeof(str);
      if (str == 0)
        printf("(null)\n");
      else
        printf("%s\n", str);
    }
    else if (fmt[i] == 'S')
    {
      uint j = 0;
      while (j < total && data[j] != '\0')
      {
        printf("%c", (unsigned char)data[j]);
        j++;
      }
      printf("\n");
      return;
    }
    else
    {
      fprintf(2, "memdump: invalid format '%c'\n", fmt[i]);
      return;
    }
  }
}
