#include "builtin.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../util/error.h"

bool is_builtin_function(const char *name) {
    const char *builtins[] = {
        "sqrt", "pow", "sin", "cos", "tan", "abs", "floor", "ceil", "round", "log", "exp",
        "length", "upper", "lower", "push", "pop",
        "trim", "replace", "substr", "indexOf",
        "first", "last", "reverse", "slice", "join",
        "read", "write", "append"
    };
    int builtin_count = sizeof(builtins) / sizeof(builtins[0]);
    for (int i = 0; i < builtin_count; i++) {
        if (strcmp(name, builtins[i]) == 0) {
            return true;
        }
    }
    return false;
}

Value call_builtin_function(const char *name, Value *args, int arg_count) {
    if (strcmp(name, "sqrt") == 0) {
        if (arg_count != 1) {
            report_error("sqrt() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        if (val < 0) {
            report_error("sqrt() of negative number");
            return make_float(0.0);
        }
        return make_float(sqrt(val));
    }

    if (strcmp(name, "pow") == 0) {
        if (arg_count != 2) {
            report_error("pow() requires 2 arguments");
            return make_float(0.0);
        }
        return make_float(pow(to_float(args[0]), to_float(args[1])));
    }

    if (strcmp(name, "sin") == 0) {
        if (arg_count != 1) {
            report_error("sin() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(sin(to_float(args[0])));
    }

    if (strcmp(name, "cos") == 0) {
        if (arg_count != 1) {
            report_error("cos() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(cos(to_float(args[0])));
    }

    if (strcmp(name, "tan") == 0) {
        if (arg_count != 1) {
            report_error("tan() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(tan(to_float(args[0])));
    }

    if (strcmp(name, "abs") == 0) {
        if (arg_count != 1) {
            report_error("abs() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        return (args[0].type == TYPE_INT) ? make_int(abs(to_int(args[0]))) : make_float(fabs(val));
    }

    if (strcmp(name, "floor") == 0) {
        if (arg_count != 1) {
            report_error("floor() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(floor(to_float(args[0])));
    }

    if (strcmp(name, "ceil") == 0) {
        if (arg_count != 1) {
            report_error("ceil() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(ceil(to_float(args[0])));
    }

    if (strcmp(name, "round") == 0) {
        if (arg_count != 1) {
            report_error("round() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(round(to_float(args[0])));
    }

    if (strcmp(name, "log") == 0) {
        if (arg_count != 1) {
            report_error("log() requires 1 argument");
            return make_float(0.0);
        }
        double val = to_float(args[0]);
        if (val <= 0) {
            report_error("log() of non-positive number");
            return make_float(0.0);
        }
        return make_float(log(val));
    }

    if (strcmp(name, "exp") == 0) {
        if (arg_count != 1) {
            report_error("exp() requires 1 argument");
            return make_float(0.0);
        }
        return make_float(exp(to_float(args[0])));
    }

    if (strcmp(name, "length") == 0) {
        if (arg_count != 1) {
            report_error("length() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type == TYPE_STRING) {
            return make_int(strlen(args[0].str_val));
        } else if (args[0].type == TYPE_ARRAY) {
            return make_int(args[0].array_val.size);
        }
        return make_int(0);
    }

    if (strcmp(name, "upper") == 0) {
        if (arg_count != 1) {
            report_error("upper() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("upper() requires a string");
            return make_string("");
        }
        char result[MAX_STRING_LEN];
        strncpy(result, args[0].str_val, MAX_STRING_LEN - 1);
        for (int i = 0; result[i]; i++) {
            result[i] = toupper(result[i]);
        }
        return make_string(result);
    }

    if (strcmp(name, "lower") == 0) {
        if (arg_count != 1) {
            report_error("lower() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("lower() requires a string");
            return make_string("");
        }
        char result[MAX_STRING_LEN];
        strncpy(result, args[0].str_val, MAX_STRING_LEN - 1);
        for (int i = 0; result[i]; i++) {
            result[i] = tolower(result[i]);
        }
        return make_string(result);
    }

    if (strcmp(name, "trim") == 0) {
        if (arg_count != 1) {
            report_error("trim() requires 1 argument");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("trim() requires a string");
            return make_string("");
        }
        const char *str = args[0].str_val;
        int start = 0;
        while (str[start] && isspace(str[start])) start++;
        int end = strlen(str) - 1;
        while (end >= start && isspace(str[end])) end--;

        char result[MAX_STRING_LEN];
        if (end < start) {
            result[0] = '\0';
        } else {
            int len = end - start + 1;
            strncpy(result, str + start, len);
            result[len] = '\0';
        }
        return make_string(result);
    }

    if (strcmp(name, "replace") == 0) {
        if (arg_count != 3) {
            report_error("replace() requires 3 arguments (string, old, new)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING || args[2].type != TYPE_STRING) {
            report_error("replace() requires string arguments");
            return make_string("");
        }

        const char *str = args[0].str_val;
        const char *old_substr = args[1].str_val;
        const char *new_substr = args[2].str_val;

        char result[MAX_STRING_LEN] = "";
        int old_len = strlen(old_substr);
        int new_len = strlen(new_substr);

        const char *p = str;
        while (*p) {
            if (strncmp(p, old_substr, old_len) == 0) {
                strncat(result, new_substr, MAX_STRING_LEN - strlen(result) - 1);
                p += old_len;
            } else {
                int len = strlen(result);
                if (len < MAX_STRING_LEN - 1) {
                    result[len] = *p;
                    result[len + 1] = '\0';
                }
                p++;
            }
        }
        return make_string(result);
    }

    if (strcmp(name, "substr") == 0) {
        if (arg_count != 3) {
            report_error("substr() requires 3 arguments (string, start, length)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("substr() requires a string as first argument");
            return make_string("");
        }

        const char *str = args[0].str_val;
        int start = to_int(args[1]);
        int len = to_int(args[2]);
        int str_len = strlen(str);

        if (start < 0 || start >= str_len || len < 0) {
            return make_string("");
        }

        if (start + len > str_len) {
            len = str_len - start;
        }

        char result[MAX_STRING_LEN];
        strncpy(result, str + start, len);
        result[len] = '\0';
        return make_string(result);
    }

    if (strcmp(name, "indexOf") == 0) {
        if (arg_count != 2) {
            report_error("indexOf() requires 2 arguments (string, substring)");
            return make_int(-1);
        }
        if (args[0].type != TYPE_STRING || args[1].type != TYPE_STRING) {
            report_error("indexOf() requires string arguments");
            return make_int(-1);
        }

        const char *str = args[0].str_val;
        const char *substr = args[1].str_val;
        const char *p = strstr(str, substr);

        if (p) {
            return make_int(p - str);
        }
        return make_int(-1);
    }

    if (strcmp(name, "first") == 0) {
        if (arg_count != 1) {
            report_error("first() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("first() on non-array or empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[0];
    }

    if (strcmp(name, "last") == 0) {
        if (arg_count != 1) {
            report_error("last() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("last() on non-array or empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[args[0].array_val.size - 1];
    }

    if (strcmp(name, "reverse") == 0) {
        if (arg_count != 1) {
            report_error("reverse() requires 1 argument");
            return make_array(0);
        }
        if (args[0].type != TYPE_ARRAY) {
            report_error("reverse() requires an array");
            return make_array(0);
        }

        Value arr = copy_value(args[0]);
        for (int i = 0; i < arr.array_val.size / 2; i++) {
            int j = arr.array_val.size - 1 - i;
            Value temp = arr.array_val.elements[i];
            arr.array_val.elements[i] = arr.array_val.elements[j];
            arr.array_val.elements[j] = temp;
        }
        return arr;
    }

    if (strcmp(name, "slice") == 0) {
        if (arg_count != 3) {
            report_error("slice() requires 3 arguments (array, start, end)");
            return make_array(0);
        }
        if (args[0].type != TYPE_ARRAY) {
            report_error("slice() requires an array");
            return make_array(0);
        }

        int start = to_int(args[1]);
        int end = to_int(args[2]);
        int size = args[0].array_val.size;

        if (start < 0) start = 0;
        if (end > size) end = size;
        if (start > end) start = end;

        int new_size = end - start;
        Value result = make_array(new_size);
        for (int i = 0; i < new_size; i++) {
            result.array_val.elements[i] = copy_value(args[0].array_val.elements[start + i]);
        }
        return result;
    }

    if (strcmp(name, "join") == 0) {
        if (arg_count != 2) {
            report_error("join() requires 2 arguments (array, separator)");
            return make_string("");
        }
        if (args[0].type != TYPE_ARRAY || args[1].type != TYPE_STRING) {
            report_error("join() requires an array and string separator");
            return make_string("");
        }

        char result[MAX_STRING_LEN] = "";
        const char *sep = args[1].str_val;
        int sep_len = strlen(sep);

        for (int i = 0; i < args[0].array_val.size; i++) {
            if (i > 0) {
                strncat(result, sep, MAX_STRING_LEN - strlen(result) - 1);
            }

            Value elem = args[0].array_val.elements[i];
            char elem_str[64];
            if (elem.type == TYPE_INT) {
                snprintf(elem_str, sizeof(elem_str), "%d", elem.int_val);
            } else if (elem.type == TYPE_FLOAT) {
                snprintf(elem_str, sizeof(elem_str), "%g", elem.float_val);
            } else if (elem.type == TYPE_STRING) {
                strncpy(elem_str, elem.str_val, sizeof(elem_str) - 1);
            } else {
                elem_str[0] = '\0';
            }
            strncat(result, elem_str, MAX_STRING_LEN - strlen(result) - 1);
        }

        return make_string(result);
    }

    if (strcmp(name, "read") == 0) {
        if (arg_count != 1) {
            report_error("read() requires 1 argument (filename)");
            return make_string("");
        }
        if (args[0].type != TYPE_STRING) {
            report_error("read() requires a string filename");
            return make_string("");
        }

        const char *filename = args[0].str_val;
        FILE *f = fopen(filename, "rb");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for reading: %s", filename);
            report_error(msg);
            return make_string("");
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size > MAX_STRING_LEN - 1) {
            size = MAX_STRING_LEN - 1;
        }

        char buffer[MAX_STRING_LEN];
        fread(buffer, 1, size, f);
        buffer[size] = '\0';
        fclose(f);

        return make_string(buffer);
    }

    if (strcmp(name, "write") == 0) {
        if (arg_count != 2) {
            report_error("write() requires 2 arguments (filename, content)");
            return make_int(0);
        }
        if (args[0].type != TYPE_STRING) {
            report_error("write() requires a string filename");
            return make_int(0);
        }

        const char *filename = args[0].str_val;
        const char *content = "";

        if (args[1].type == TYPE_STRING) {
            content = args[1].str_val;
        } else {
            static char temp_str[64];
            if (args[1].type == TYPE_INT) {
                snprintf(temp_str, sizeof(temp_str), "%d", args[1].int_val);
            } else if (args[1].type == TYPE_FLOAT) {
                snprintf(temp_str, sizeof(temp_str), "%g", args[1].float_val);
            }
            content = temp_str;
        }

        FILE *f = fopen(filename, "wb");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for writing: %s", filename);
            report_error(msg);
            return make_int(0);
        }

        fwrite(content, 1, strlen(content), f);
        fclose(f);

        return make_int(strlen(content));
    }

    if (strcmp(name, "append") == 0) {
        if (arg_count != 2) {
            report_error("append() requires 2 arguments (filename, content)");
            return make_int(0);
        }
        if (args[0].type != TYPE_STRING) {
            report_error("append() requires a string filename");
            return make_int(0);
        }

        const char *filename = args[0].str_val;
        const char *content = "";

        if (args[1].type == TYPE_STRING) {
            content = args[1].str_val;
        } else {
            static char temp_str[64];
            if (args[1].type == TYPE_INT) {
                snprintf(temp_str, sizeof(temp_str), "%d", args[1].int_val);
            } else if (args[1].type == TYPE_FLOAT) {
                snprintf(temp_str, sizeof(temp_str), "%g", args[1].float_val);
            }
            content = temp_str;
        }

        FILE *f = fopen(filename, "ab");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot open file for appending: %s", filename);
            report_error(msg);
            return make_int(0);
        }

        fwrite(content, 1, strlen(content), f);
        fclose(f);

        return make_int(strlen(content));
    }

    if (strcmp(name, "push") == 0) {
        if (arg_count != 2) {
            report_error("push() requires 2 arguments (array, value)");
            return make_int(0);
        }
        return make_int(args[0].array_val.size);
    }

    if (strcmp(name, "pop") == 0) {
        if (arg_count != 1) {
            report_error("pop() requires 1 argument");
            return make_int(0);
        }
        if (args[0].type != TYPE_ARRAY || args[0].array_val.size == 0) {
            report_error("pop() on empty array");
            return make_int(0);
        }
        return args[0].array_val.elements[args[0].array_val.size - 1];
    }

    report_error("Unknown built-in function");
    return make_int(0);
}
