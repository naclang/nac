#include "eval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../builtin/builtin.h"
#include "../builtin/extended_builtin.h"
#include "../core/interpreter.h"
#include "../net/http.h"
#include "../parser/parser.h"
#include "../util/error.h"
#include "vartable.h"

static int key_from_value(Value key_val, char *buffer, size_t buffer_size) {
    if (key_val.type == TYPE_STRING) {
        strncpy(buffer, key_val.str_val, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return 1;
    }

    if (key_val.type == TYPE_INT) {
        snprintf(buffer, buffer_size, "%d", key_val.int_val);
        return 1;
    }

    if (key_val.type == TYPE_FLOAT) {
        snprintf(buffer, buffer_size, "%g", key_val.float_val);
        return 1;
    }

    return 0;
}

Value eval_node(ASTNode *node) {
    if (!node) return make_int(0);

    switch (node->type) {
        case AST_INT_LITERAL:
            return make_int(node->int_val);

        case AST_FLOAT_LITERAL:
            return make_float(node->float_val);

        case AST_STRING_LITERAL:
            return make_string(node->str_val);

        case AST_VARIABLE: {
            Value *v = get_var(node->var_name);
            if (!v) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable: %s", node->var_name);
                report_error(msg);
                return make_int(0);
            }
            return *v;
        }

        case AST_ARRAY_ACCESS: {
            Value *arr = get_var(node->array_access.var_name);
            if (!arr) {
                report_error("Undefined indexed variable");
                return make_int(0);
            }

            Value idx_val = eval_node(node->array_access.index);

            if (arr->type == TYPE_ARRAY) {
                int idx = to_int(idx_val);

                if (idx < 0 || idx >= arr->array_val.size) {
                    report_error("Array index out of bounds");
                    return make_int(0);
                }

                return arr->array_val.elements[idx];
            }

            if (arr->type == TYPE_MAP) {
                char key[MAX_STRING_LEN];
                if (!key_from_value(idx_val, key, sizeof(key))) {
                    report_error("Map key must be int, float, or string");
                    return make_int(0);
                }

                Value *found = map_get(arr, key);
                if (!found) {
                    report_error("Map key not found");
                    return make_int(0);
                }
                return *found;
            }

            report_error("Variable is not indexable");
            return make_int(0);
        }

        case AST_BINARY_OP: {
            Value left = eval_node(node->binary.left);
            Value right = eval_node(node->binary.right);

            switch (node->binary.op) {
                case TOK_PLUS:
                    if (left.type == TYPE_STRING || right.type == TYPE_STRING) {
                        char result[MAX_STRING_LEN];
                        if (left.type == TYPE_STRING) {
                            strncpy(result, left.str_val, MAX_STRING_LEN - 1);
                        } else {
                            snprintf(result, MAX_STRING_LEN, "%g", to_float(left));
                        }

                        int len = strlen(result);
                        if (right.type == TYPE_STRING) {
                            strncat(result, right.str_val, MAX_STRING_LEN - len - 1);
                        } else {
                            char temp[64];
                            snprintf(temp, sizeof(temp), "%g", to_float(right));
                            strncat(result, temp, MAX_STRING_LEN - len - 1);
                        }
                        return make_string(result);
                    }
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) + to_float(right));
                    }
                    return make_int(to_int(left) + to_int(right));

                case TOK_MINUS:
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) - to_float(right));
                    }
                    return make_int(to_int(left) - to_int(right));

                case TOK_STAR:
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) * to_float(right));
                    }
                    return make_int(to_int(left) * to_int(right));

                case TOK_SLASH:
                    if (to_float(right) == 0) {
                        report_error("Division by zero");
                        return make_int(0);
                    }
                    if (left.type == TYPE_FLOAT || right.type == TYPE_FLOAT) {
                        return make_float(to_float(left) / to_float(right));
                    }
                    return make_int(to_int(left) / to_int(right));

                case TOK_PERCENT:
                    if (to_int(right) == 0) {
                        report_error("Modulo by zero");
                        return make_int(0);
                    }
                    return make_int(to_int(left) % to_int(right));

                case TOK_EQ:
                    return make_int(to_float(left) == to_float(right));
                case TOK_NEQ:
                    return make_int(to_float(left) != to_float(right));
                case TOK_LT:
                    return make_int(to_float(left) < to_float(right));
                case TOK_GT:
                    return make_int(to_float(left) > to_float(right));
                case TOK_LTE:
                    return make_int(to_float(left) <= to_float(right));
                case TOK_GTE:
                    return make_int(to_float(left) >= to_float(right));
                case TOK_AND:
                    return make_int(to_bool(left) && to_bool(right));
                case TOK_OR:
                    return make_int(to_bool(left) || to_bool(right));

                default:
                    report_error("Unknown binary operator");
                    return make_int(0);
            }
        }

        case AST_UNARY_OP: {
            Value operand = eval_node(node->unary.operand);
            switch (node->unary.op) {
                case TOK_MINUS:
                    if (operand.type == TYPE_FLOAT) {
                        return make_float(-operand.float_val);
                    }
                    return make_int(-to_int(operand));
                case TOK_NOT:
                    return make_int(!to_bool(operand));
                default:
                    return make_int(0);
            }
        }

        case AST_ASSIGN: {
            Value val = eval_node(node->assign.value);
            set_var(node->assign.var_name, val);
            return val;
        }

        case AST_ARRAY_ASSIGN: {
            Value *arr = get_var(node->array_assign.var_name);
            if (!arr) {
                report_error("Undefined indexed variable");
                return make_int(0);
            }

            Value idx_val = eval_node(node->array_assign.index);
            Value val = eval_node(node->array_assign.value);

            if (arr->type == TYPE_ARRAY) {
                int idx = to_int(idx_val);

                if (idx < 0 || idx >= arr->array_val.size) {
                    report_error("Array index out of bounds");
                    return make_int(0);
                }

                free_value(&arr->array_val.elements[idx]);
                arr->array_val.elements[idx] = copy_value(val);
                return val;
            }

            if (arr->type == TYPE_MAP) {
                char key[MAX_STRING_LEN];
                if (!key_from_value(idx_val, key, sizeof(key))) {
                    report_error("Map key must be int, float, or string");
                    return make_int(0);
                }

                map_set(arr, key, val);
                return val;
            }

            report_error("Variable is not indexable");
            return make_int(0);
        }

        case AST_CALL: {
            Value *arg_values = (Value*)malloc(sizeof(Value) * node->call.arg_count);
            for (int i = 0; i < node->call.arg_count; i++) {
                arg_values[i] = eval_node(node->call.args[i]);
            }

            if (is_builtin_function(node->call.func_name)) {
                Value result = call_builtin_function(node->call.func_name, arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            if (is_extended_builtin(node->call.func_name)) {
                Value result = call_extended_builtin(node->call.func_name, arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            Function *func = NULL;
            for (int i = 0; i < func_count; i++) {
                if (strcmp(functions[i].name, node->call.func_name) == 0) {
                    func = &functions[i];
                    break;
                }
            }

            if (!func) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined function: %s", node->call.func_name);
                report_error(msg);
                free(arg_values);
                return make_int(0);
            }

            if (node->call.arg_count != func->param_count) {
                report_error("Argument count mismatch");
                free(arg_values);
                return make_int(0);
            }

            if (call_depth >= MAX_CALL_DEPTH) {
                free(arg_values);
                report_error("Stack overflow");
                return make_int(0);
            }

            call_stack_vars[call_depth] = create_var_table();
            call_depth++;

            for (int i = 0; i < func->param_count; i++) {
                set_var(func->params[i], arg_values[i]);
            }
            free(arg_values);

            should_return = false;
            eval_node(func->body);

            Value result = return_value;
            should_return = false;

            call_depth--;
            free_var_table(call_stack_vars[call_depth]);
            call_stack_vars[call_depth] = NULL;

            return result;
        }

        case AST_BLOCK: {
            for (int i = 0; i < node->block.count; i++) {
                if (should_break || should_continue || should_return) break;
                eval_node(node->block.statements[i]);
            }
            return make_int(0);
        }

        case AST_IF: {
            Value condition = eval_node(node->if_stmt.condition);
            if (to_bool(condition)) {
                eval_node(node->if_stmt.then_block);
            } else if (node->if_stmt.else_block) {
                eval_node(node->if_stmt.else_block);
            }
            return make_int(0);
        }

        case AST_FOR: {
            if (node->for_stmt.init) {
                eval_node(node->for_stmt.init);
            }

            while (1) {
                Value condition = eval_node(node->for_stmt.condition);
                if (!to_bool(condition)) break;

                should_continue = false;
                eval_node(node->for_stmt.body);

                if (should_break) {
                    should_break = false;
                    break;
                }
                if (should_return) break;

                if (node->for_stmt.increment) {
                    eval_node(node->for_stmt.increment);
                }
            }

            should_continue = false;
            return make_int(0);
        }

        case AST_WHILE: {
            while (1) {
                Value condition = eval_node(node->while_stmt.condition);
                if (!to_bool(condition)) break;

                should_continue = false;
                eval_node(node->while_stmt.body);

                if (should_break) {
                    should_break = false;
                    break;
                }
                if (should_return) break;
            }

            should_continue = false;
            return make_int(0);
        }

        case AST_HTTP: {
            Value method_val = eval_node(node->http_stmt.method);
            Value url_val = eval_node(node->http_stmt.url);

            if (method_val.type != TYPE_STRING || url_val.type != TYPE_STRING) {
                report_error("http() requires string arguments");
                return make_int(0);
            }

            const char *body_str = NULL;
            if (node->http_stmt.body) {
                Value body_val = eval_node(node->http_stmt.body);
                if (body_val.type == TYPE_STRING) {
                    body_str = body_val.str_val;
                }
            }

#ifdef _WIN32
            http_request_win(method_val.str_val, url_val.str_val, body_str);
#else
            http_request_unix(method_val.str_val, url_val.str_val, body_str);
#endif

            return make_int(0);
        }

        case AST_RETURN: {
            Value val = eval_node(node->return_stmt.value);
            return_value = copy_value(val);
            should_return = true;
            return return_value;
        }

        case AST_BREAK:
            should_break = true;
            return make_int(0);

        case AST_CONTINUE:
            should_continue = true;
            return make_int(0);

        case AST_OUT: {
            Value val = eval_node(node->out_stmt.value);
            print_value(val);
            return make_int(0);
        }

        case AST_IN: {
            char input[MAX_STRING_LEN];
            if (fgets(input, MAX_STRING_LEN, stdin)) {
                input[strcspn(input, "\n")] = '\0';

                char *endptr;
                long int_val = strtol(input, &endptr, 10);
                Value result;
                if (*endptr == '\0') {
                    result = make_int(int_val);
                } else {
                    double float_val = strtod(input, &endptr);
                    if (*endptr == '\0') {
                        result = make_float(float_val);
                    } else {
                        result = make_string(input);
                    }
                }

                if (strcmp(node->in_stmt.var_name, "__temp_in") != 0) {
                    set_var(node->in_stmt.var_name, result);
                }

                return result;
            }
            return make_int(0);
        }

        case AST_INCREMENT: {
            Value *v = get_var(node->inc_dec.var_name);
            if (!v) {
                report_error("Undefined variable");
                return make_int(0);
            }
            if (v->type == TYPE_FLOAT) {
                Value new_val = make_float(v->float_val + 1);
                set_var(node->inc_dec.var_name, new_val);
            } else {
                Value new_val = make_int(to_int(*v) + 1);
                set_var(node->inc_dec.var_name, new_val);
            }
            return make_int(0);
        }

        case AST_DECREMENT: {
            Value *v = get_var(node->inc_dec.var_name);
            if (!v) {
                report_error("Undefined variable");
                return make_int(0);
            }
            if (v->type == TYPE_FLOAT) {
                Value new_val = make_float(v->float_val - 1);
                set_var(node->inc_dec.var_name, new_val);
            } else {
                Value new_val = make_int(to_int(*v) - 1);
                set_var(node->inc_dec.var_name, new_val);
            }
            return make_int(0);
        }

        case AST_ARRAY_LITERAL: {
            if (node->array_literal.count == 1) {
                Value size_val = eval_node(node->array_literal.elements[0]);
                int size = to_int(size_val);
                if (size < 0 || size > MAX_ARRAY_SIZE) {
                    report_error("Invalid array size");
                    return make_int(0);
                }
                return make_array(size);
            } else {
                Value arr = make_array(node->array_literal.count);
                for (int i = 0; i < node->array_literal.count; i++) {
                    arr.array_val.elements[i] = eval_node(node->array_literal.elements[i]);
                }
                return arr;
            }
        }

        default:
            return make_int(0);
    }
}




