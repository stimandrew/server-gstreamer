#include "file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

#define MAX_TEXT_LINE_LENGTH 1024

int read_data_from_file(const char *path, std::vector<char>& out_data)
{
    FILE *fp = fopen(path, "rb");
    if(fp == NULL) {
        printf("fopen %s fail!\n", path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    out_data.resize(file_size);
    fseek(fp, 0, SEEK_SET);
    if(file_size != fread(out_data.data(), 1, file_size, fp)) {
        printf("fread %s fail!\n", path);
        out_data.clear();
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return file_size;
}

int write_data_to_file(const char *path, const char *data, unsigned int size)
{
    FILE *fp = fopen(path, "w");
    if(fp == NULL) {
        printf("open error: %s\n", path);
        return -1;
    }

    fwrite(data, 1, size, fp);
    fflush(fp);
    fclose(fp);
    return 0;
}

static int count_lines(FILE* file)
{
    int count = 0;
    char ch;

    while(!feof(file))
    {
        ch = fgetc(file);
        if(ch == '\n')
        {
            count++;
        }
    }
    count += 1;

    rewind(file);
    return count;
}

std::vector<std::string> read_lines_from_file(const char* filename, int* line_count)
{
    std::vector<std::string> lines;
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Failed to open the file.\n");
        *line_count = 0;
        return lines;
    }

    *line_count = count_lines(file);
    printf("num_lines=%d\n", *line_count);
    lines.reserve(*line_count);

    char buffer[MAX_TEXT_LINE_LENGTH];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';  // Remove newline
        lines.emplace_back(buffer);
    }

    fclose(file);
    return lines;
}
