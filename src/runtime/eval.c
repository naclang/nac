#include "eval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../builtin/builtin.h"
#include "../builtin/db_builtin.h"
#include "../builtin/extended_builtin.h"
#include "../builtin/security_builtin.h"
#include "../builtin/time_builtin.h"
#include "../core/interpreter.h"
#include "../net/http.h"
#include "../parser/parser.h"
#include "../util/error.h"
#include "vartable.h"

/* Marks `v` as a borrowed view: free_value() must not release storage that
 * belongs to a variable or container. */
static Value borrow_value(Value v) {
    v.owned = 0;
    return v;
}

/* Moves an owned temporary into a container slot, deep-copying borrowed
 * views so the slot never aliases another container. */
static Value take_or_copy(Value v) {
    if (v.owned) return v;
    return copy_value(v);
}

static void free_value_args(Value *args, int count) {
    for (int i = 0; i < count; i++) {
        free_value(&args[i]);
    }
}

static char *display_dup(Value v) {
    if (v.type == TYPE_STRING) {
        const char *s = v.str_val ? v.str_val : "";
        size_t len = strlen(s);
        char *copy = (char*)malloc(len + 1);
        if (copy) memcpy(copy, s, len + 1);
        return copy;
    }

    char tmp[256];
    value_to_display_string(v, tmp, sizeof(tmp));
    size_t len = strlen(tmp);
    char *copy = (char*)malloc(len + 1);
    if (copy) memcpy(copy, tmp, len + 1);
    return copy;
}

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
        nac_format_double(key_val.float_val, buffer, buffer_size);
        return 1;
    }

    if (key_val.type == TYPE_BOOL) {
        snprintf(buffer, buffer_size, "%s", key_val.int_val ? "true" : "false");
        return 1;
    }

    return 0;
}

int call_nac_function(const char *name, Value *args, int arg_count, Value *out_result) {
    Function *func = NULL;
    for (int i = 0; i < func_count; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            func = &functions[i];
            break;
        }
    }

    if (!func) {
        return 0;
    }

    if (arg_count != func->param_count) {
        report_error("Argument count mismatch calling function from host");
        *out_result = make_int(0);
        return 1;
    }

    if (call_depth >= MAX_CALL_DEPTH) {
        report_error("Stack overflow");
        *out_result = make_int(0);
        return 1;
    }

    call_stack_vars[call_depth] = create_var_table();
    call_depth++;

    for (int i = 0; i < func->param_count; i++) {
        declare_local_var(func->params[i], args[i]);
    }

    bool prev_should_return = should_return;
    should_return = false;

    free_value(&return_value);
    return_value = make_null();
    eval_node(func->body);

    Value result = return_value;
    return_value = make_null();
    should_return = prev_should_return;

    call_depth--;
    free_var_table(call_stack_vars[call_depth]);
    call_stack_vars[call_depth] = NULL;

    *out_result = result;
    return 1;
}

static Function *find_function(const char *name) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(functions[i].name, name) == 0) return &functions[i];
    }
    return NULL;
}

int call_func_value(Value fn, Value *args, int arg_count, Value *out_result) {
    if (fn.type != TYPE_FUNC) return 0;
    Function *func = find_function(fn.str_val);
    if (!func) return 0;

    if (call_depth >= MAX_CALL_DEPTH) {
        report_error("Stack overflow");
        *out_result = make_null();
        return 1;
    }

    call_stack_vars[call_depth] = create_var_table();
    call_depth++;

    for (int i = 0; i < func->param_count; i++) {
        declare_local_var(func->params[i], i < arg_count ? args[i] : make_null());
    }

    bool prev_should_return = should_return;
    should_return = false;
    free_value(&return_value);
    return_value = make_null();   /* a callback without `return` yields null */

    eval_node(func->body);

    Value result = return_value;
    return_value = make_null();
    should_return = prev_should_return;

    call_depth--;
    free_var_table(call_stack_vars[call_depth]);
    call_stack_vars[call_depth] = NULL;

    *out_result = result;
    return 1;
}

/* Shared by AST_BINARY_OP and compound assignment (+=, -=, *=, /=) so both
 * paths get identical semantics (e.g. string concatenation on '+'). */
static Value apply_arith_op(NaCTokenType op, Value left, Value right) {
    switch (op) {
        case TOK_PLUS:
            if (left.type == TYPE_STRING || right.type == TYPE_STRING) {
                /* Non-string operands are rendered like print() would show
                 * them (true/false/null, arrays, maps), not as raw numbers. */
                char *left_s = display_dup(left);
                char *right_s = display_dup(right);
                if (!left_s || !right_s) {
                    free(left_s);
                    free(right_s);
                    report_error("Out of memory concatenating strings");
                    return make_string("");
                }

                size_t left_len = strlen(left_s);
                size_t right_len = strlen(right_s);
                char *result = (char*)malloc(left_len + right_len + 1);
                if (!result) {
                    free(left_s);
                    free(right_s);
                    report_error("Out of memory concatenating strings");
                    return make_string("");
                }
                memcpy(result, left_s, left_len);
                memcpy(result + left_len, right_s, right_len + 1);
                free(left_s);
                free(right_s);
                return make_string_take(result);
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

        default:
            report_error("Unknown compound assignment operator");
            return right;
    }
}

static int values_equal(Value a, Value b) {
    if (a.type == TYPE_NULL || b.type == TYPE_NULL) return a.type == b.type;
    if (a.type == TYPE_STRING && b.type == TYPE_STRING) return strcmp(a.str_val, b.str_val) == 0;
    int a_num = (a.type == TYPE_INT || a.type == TYPE_FLOAT || a.type == TYPE_BOOL);
    int b_num = (b.type == TYPE_INT || b.type == TYPE_FLOAT || b.type == TYPE_BOOL);
    if (a_num && b_num) return to_float(a) == to_float(b);
    return 0;
}

/* Resolves an assignable expression (variable or a chain of indexes) to a
 * pointer into the live storage, so nested writes like a[i][j] = v or
 * m.a.b = v modify the original containers in place. */
static Value *lvalue_ptr(ASTNode *n) {
    if (n->type == AST_VARIABLE) {
        return get_var(n->var_name);
    }
    if (n->type == AST_INDEX) {
        Value *base = lvalue_ptr(n->index.object);
        if (!base) return NULL;
        Value idx_val = eval_node(n->index.index);
        if (error_pending) {
            free_value(&idx_val);
            return NULL;
        }

        if (base->type == TYPE_ARRAY) {
            int idx = to_int(idx_val);
            free_value(&idx_val);
            if (idx < 0 || idx >= base->array_val.size) {
                report_error("Array index out of bounds");
                return NULL;
            }
            return &base->array_val.elements[idx];
        }
        if (base->type == TYPE_MAP) {
            char key[1024];
            int ok = key_from_value(idx_val, key, sizeof(key));
            free_value(&idx_val);
            if (!ok) return NULL;
            return map_get(base, key);
        }
        free_value(&idx_val);
    }
    return NULL;
}

Value eval_node(ASTNode *node) {
    if (!node) return make_int(0);

    switch (node->type) {
        case AST_INT_LITERAL:
            return make_int(node->int_val);

        case AST_BOOL_LITERAL:
            return make_bool(node->int_val);

        case AST_NULL_LITERAL:
            return make_null();

        case AST_FLOAT_LITERAL:
            return make_float(node->float_val);

        case AST_STRING_LITERAL:
            return make_string(node->str_val);

        case AST_VARIABLE: {
            Value *v = get_var(node->var_name);
            if (!v && find_function(node->var_name)) {
                /* `callback: onDone` -- a declared function used as a value */
                return make_func(node->var_name);
            }
            if (!v) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined variable: %s", node->var_name);
                report_error(msg);
                return make_null();
            }
            return borrow_value(*v);
        }

        case AST_BINARY_OP: {
            Value left = eval_node(node->binary.left);

            /* and / or short-circuit, like JavaScript and Python */
            if (node->binary.op == TOK_AND && !to_bool(left)) return left;
            if (node->binary.op == TOK_OR && to_bool(left)) return left;

            Value right = eval_node(node->binary.right);
            Value result;

            switch (node->binary.op) {
                case TOK_PLUS:
                case TOK_MINUS:
                case TOK_STAR:
                case TOK_SLASH:
                    result = apply_arith_op(node->binary.op, left, right);
                    break;

                case TOK_PERCENT:
                    if (to_int(right) == 0) {
                        report_error("Modulo by zero");
                        result = make_int(0);
                    } else {
                        result = make_int(to_int(left) % to_int(right));
                    }
                    break;

                case TOK_EQ:
                    if (left.type == TYPE_NULL || right.type == TYPE_NULL) {
                        result = make_bool(left.type == TYPE_NULL && right.type == TYPE_NULL);
                    } else if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        result = make_bool(strcmp(left.str_val, right.str_val) == 0);
                    } else {
                        result = make_bool(to_float(left) == to_float(right));
                    }
                    break;
                case TOK_NEQ:
                    if (left.type == TYPE_NULL || right.type == TYPE_NULL) {
                        result = make_bool(!(left.type == TYPE_NULL && right.type == TYPE_NULL));
                    } else if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        result = make_bool(strcmp(left.str_val, right.str_val) != 0);
                    } else {
                        result = make_bool(to_float(left) != to_float(right));
                    }
                    break;
                case TOK_LT:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        result = make_bool(strcmp(left.str_val, right.str_val) < 0);
                    } else {
                        result = make_bool(to_float(left) < to_float(right));
                    }
                    break;
                case TOK_GT:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        result = make_bool(strcmp(left.str_val, right.str_val) > 0);
                    } else {
                        result = make_bool(to_float(left) > to_float(right));
                    }
                    break;
                case TOK_LTE:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        result = make_bool(strcmp(left.str_val, right.str_val) <= 0);
                    } else {
                        result = make_bool(to_float(left) <= to_float(right));
                    }
                    break;
                case TOK_GTE:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        result = make_bool(strcmp(left.str_val, right.str_val) >= 0);
                    } else {
                        result = make_bool(to_float(left) >= to_float(right));
                    }
                    break;
                case TOK_IN: {
                    result = make_bool(0);
                    if (right.type == TYPE_ARRAY) {
                        for (int i = 0; i < right.array_val.size; i++) {
                            if (values_equal(left, right.array_val.elements[i])) {
                                result = make_bool(1);
                                break;
                            }
                        }
                    } else if (right.type == TYPE_MAP) {
                        char key[1024];
                        if (key_from_value(left, key, sizeof(key))) {
                            result = make_bool(map_get(&right, key) != NULL);
                        }
                    } else if (right.type == TYPE_STRING && left.type == TYPE_STRING) {
                        result = make_bool(strstr(right.str_val, left.str_val) != NULL);
                    } else {
                        report_error("'in' needs an array, map or string on the right");
                    }
                    break;
                }
                case TOK_AND:   /* left was truthy -> result is the right operand */
                case TOK_OR:    /* left was falsy  -> result is the right operand */
                    free_value(&left);
                    return right;

                default:
                    report_error("Unknown binary operator");
                    result = make_int(0);
                    break;
            }

            free_value(&left);
            free_value(&right);
            return result;
        }

        case AST_UNARY_OP: {
            Value operand = eval_node(node->unary.operand);
            Value result;
            switch (node->unary.op) {
                case TOK_MINUS:
                    result = (operand.type == TYPE_FLOAT)
                                 ? make_float(-operand.float_val)
                                 : make_int(-to_int(operand));
                    break;
                case TOK_NOT:
                    result = make_bool(!to_bool(operand));
                    break;
                default:
                    result = make_int(0);
                    break;
            }
            free_value(&operand);
            return result;
        }

        case AST_ASSIGN: {
            Value val = eval_node(node->assign.value);

            if (node->assign.compound_op != TOK_EOF) {
                Value *current = get_var(node->assign.var_name);
                if (!current) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Undefined variable: %s", node->assign.var_name);
                    report_error(msg);
                    free_value(&val);
                    return make_null();
                }
                Value combined = apply_arith_op(node->assign.compound_op, *current, val);
                free_value(&val);
                val = combined;
            }

            if (error_pending) {   /* failed inside try: leave the variable alone */
                free_value(&val);
                return make_null();
            }

            /* set_var() copies before releasing the old value, so
             * `a = a[0]` / `a = a.child` stays safe without an extra copy. */
            set_var(node->assign.var_name, val);
            return val;
        }

        case AST_FUNC_LITERAL:
            return make_func(node->var_name);

        case AST_TRY: {
            try_depth++;
            eval_node(node->try_stmt.try_block);
            try_depth--;

            if (error_pending) {
                char message[512];
                snprintf(message, sizeof(message), "%s", caught_error);
                error_pending = false;
                caught_error[0] = '\0';
                if (node->try_stmt.var_name[0]) {
                    Value caught = make_string(message);
                    set_var(node->try_stmt.var_name, caught);
                    free_value(&caught);
                }
                eval_node(node->try_stmt.catch_block);
            }
            return make_int(0);
        }

        case AST_THROW: {
            Value v = eval_node(node->throw_stmt.value);
            if (error_pending) { free_value(&v); return make_int(0); }
            char message[512];
            value_to_display_string(v, message, sizeof(message));
            free_value(&v);
            if (try_depth > 0) {
                report_error(message);        /* captured by the enclosing try */
            } else {
                char full[600];
                snprintf(full, sizeof(full), "Uncaught: %s", message);
                error_and_exit(full);
            }
            return make_int(0);
        }

        case AST_HTTP_CALL: {
            Value opts = eval_node(node->http_call.data);
            if (error_pending) { free_value(&opts); return make_null(); }
            Value res = http_call_with_options(node->http_call.method, opts);
            free_value(&opts);
            return res;
        }

        case AST_CALL: {
            /* f(x) where `f` is a variable holding a function value
             * (f = fn(x) { ... }  /  f = someDeclaredFn): call what it
             * refers to. */
            char target_name[MAX_TOKEN_LEN];
            strncpy(target_name, node->call.func_name, MAX_TOKEN_LEN - 1);
            target_name[MAX_TOKEN_LEN - 1] = '\0';
            bool via_variable = false;
            {
                Value *fv = get_var(node->call.func_name);
                if (fv && fv->type == TYPE_FUNC) {
                    strncpy(target_name, fv->str_val, MAX_TOKEN_LEN - 1);
                    target_name[MAX_TOKEN_LEN - 1] = '\0';
                    via_variable = true;
                }
            }

            Value *arg_values = NULL;
            if (node->call.arg_count > 0) {
                arg_values = (Value*)calloc((size_t)node->call.arg_count, sizeof(Value));
                if (!arg_values) {
                    report_error("Out of memory");
                    return make_null();
                }
            }

            bool is_mutating_array_call =
                (strcmp(node->call.func_name, "push") == 0 || strcmp(node->call.func_name, "pop") == 0 ||
                 strcmp(node->call.func_name, "removeAt") == 0 || strcmp(node->call.func_name, "delete") == 0) &&
                node->call.arg_count > 0 && node->call.args[0]->type == AST_VARIABLE;

            for (int i = 0; i < node->call.arg_count; i++) {
                if (i == 0 && is_mutating_array_call) {
                    /* push()/pop() realloc/free the array buffer in place, so
                     * they must operate on an independent copy rather than
                     * the pointer shared with the stored variable. */
                    Value *src = get_var(node->call.args[0]->var_name);
                    arg_values[0] = src ? copy_value(*src) : make_null();
                } else {
                    arg_values[i] = eval_node(node->call.args[i]);
                }
                if (error_pending) break;
            }

            if (error_pending) {      /* an argument failed inside try: don't run the call */
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return make_null();
            }

            if (!via_variable && is_builtin_function(node->call.func_name)) {
                Value result = call_builtin_function(node->call.func_name, arg_values, node->call.arg_count);

                if (is_mutating_array_call) {
                    set_var(node->call.args[0]->var_name, arg_values[0]);
                    free_value(&arg_values[0]);
                    arg_values[0] = make_null();
                }

                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            if (!via_variable && is_extended_builtin(node->call.func_name)) {
                Value result = call_extended_builtin(node->call.func_name, arg_values, node->call.arg_count);
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            if (!via_variable && is_security_builtin(node->call.func_name)) {
                Value result = call_security_builtin(node->call.func_name, arg_values, node->call.arg_count);
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            if (!via_variable && is_time_builtin(node->call.func_name)) {
                Value result = call_time_builtin(node->call.func_name, arg_values, node->call.arg_count);
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            if (!via_variable && is_db_builtin(node->call.func_name)) {
                Value result = call_db_builtin(node->call.func_name, arg_values, node->call.arg_count);
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            Function *func = find_function(target_name);

            if (!func) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined function: %s", node->call.func_name);
                report_error(msg);
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return make_int(0);
            }

            if (node->call.arg_count != func->param_count) {
                report_error("Argument count mismatch");
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return make_int(0);
            }

            if (call_depth >= MAX_CALL_DEPTH) {
                report_error("Stack overflow");
                free_value_args(arg_values, node->call.arg_count);
                free(arg_values);
                return make_int(0);
            }

            call_stack_vars[call_depth] = create_var_table();
            call_depth++;

            for (int i = 0; i < func->param_count; i++) {
                declare_local_var(func->params[i], arg_values[i]);
            }
            free_value_args(arg_values, node->call.arg_count);
            free(arg_values);

            free_value(&return_value);
            return_value = make_null();
            should_return = false;
            eval_node(func->body);

            Value result = return_value;
            return_value = make_null();
            should_return = false;

            call_depth--;
            free_var_table(call_stack_vars[call_depth]);
            call_stack_vars[call_depth] = NULL;

            return result;
        }

        case AST_BLOCK: {
            for (int i = 0; i < node->block.count; i++) {
                if (should_break || should_continue || should_return || error_pending) break;
                Value stmt = eval_node(node->block.statements[i]);
                /* A `return` hands its value over through return_value; every
                 * other statement result is a temporary we can release. */
                if (!should_return) free_value(&stmt);
            }
            return make_int(0);
        }

        case AST_IF: {
            Value condition = eval_node(node->if_stmt.condition);
            int truthy = !error_pending && to_bool(condition);
            free_value(&condition);
            if (error_pending) return make_int(0);
            if (truthy) {
                eval_node(node->if_stmt.then_block);
            } else if (node->if_stmt.else_block) {
                eval_node(node->if_stmt.else_block);
            }
            return make_int(0);
        }

        case AST_WHILE: {
            while (1) {
                Value condition = eval_node(node->while_stmt.condition);
                int truthy = !error_pending && to_bool(condition);
                free_value(&condition);
                if (error_pending || !truthy) break;

                should_continue = false;
                eval_node(node->while_stmt.body);

                if (should_break) {
                    should_break = false;
                    break;
                }
                if (should_return || error_pending) break;
            }

            should_continue = false;
            return make_int(0);
        }

        case AST_RETURN: {
            Value val = eval_node(node->return_stmt.value);
            if (error_pending) {
                free_value(&val);
                return make_null();
            }
            free_value(&return_value);
            return_value = copy_value(val);
            free_value(&val);
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
            if (error_pending) {
                free_value(&val);
                return make_int(0);   /* an error inside try: don't print */
            }
            print_value(val);
            free_value(&val);
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
            Value arr = make_array(node->array_literal.count);
            for (int i = 0; i < node->array_literal.count; i++) {
                Value item = eval_node(node->array_literal.elements[i]);
                arr.array_val.elements[i] = take_or_copy(item);
            }
            return arr;
        }

        case AST_MAP_LITERAL: {
            Value m = make_map();
            for (int i = 0; i < node->map_literal.count; i++) {
                Value k = eval_node(node->map_literal.keys[i]);
                char key[1024];
                int key_ok = key_from_value(k, key, sizeof(key));
                free_value(&k);
                if (!key_ok) {
                    report_error("Map key must be int, float, string, or bool");
                    continue;
                }
                Value v = eval_node(node->map_literal.values[i]);
                map_set(&m, key, v);
                free_value(&v);
            }
            return m;
        }

        case AST_INDEX: {
            Value base = eval_node(node->index.object);
            Value idx_val = eval_node(node->index.index);
            Value result = make_null();

            if (base.type == TYPE_ARRAY) {
                int idx = to_int(idx_val);
                if (idx < 0 || idx >= base.array_val.size) {
                    report_error("Array index out of bounds");
                } else if (base.owned) {
                    /* A temporary container (an array literal): hand the
                     * element out and release the container. */
                    result = copy_value(base.array_val.elements[idx]);
                } else {
                    result = borrow_value(base.array_val.elements[idx]);
                }
            } else if (base.type == TYPE_MAP) {
                char key[1024];
                if (!key_from_value(idx_val, key, sizeof(key))) {
                    report_error("Map key must be int, float, string, or bool");
                } else {
                    Value *found = map_get(&base, key);
                    if (found) {
                        result = base.owned ? copy_value(*found) : borrow_value(*found);
                    }
                }
            } else if (base.type == TYPE_STRING) {
                int idx = to_int(idx_val);
                int len = (int)strlen(base.str_val);
                if (idx < 0 || idx >= len) {
                    report_error("String index out of bounds");
                } else {
                    char one[2] = { base.str_val[idx], '\0' };
                    result = make_string(one);
                }
            } else {
                report_error("Value is not indexable");
            }

            free_value(&idx_val);
            free_value(&base);
            return result;
        }

        case AST_INDEX_ASSIGN: {
            ASTNode *t = node->index_assign.target;
            Value val = eval_node(node->index_assign.value);
            if (error_pending) { free_value(&val); return make_null(); }
            Value *container = lvalue_ptr(t->index.object);
            if (!container) {
                report_error("Cannot assign: the container is undefined or missing");
                free_value(&val);
                return make_null();
            }
            Value idx_val = eval_node(t->index.index);
            if (error_pending) { free_value(&idx_val); free_value(&val); return make_null(); }
            NaCTokenType cop = node->index_assign.compound_op;

            if (container->type == TYPE_ARRAY) {
                int idx = to_int(idx_val);
                free_value(&idx_val);
                if (idx < 0 || idx >= container->array_val.size) {
                    report_error("Array index out of bounds");
                    free_value(&val);
                    return make_null();
                }
                if (cop != TOK_EOF) {
                    Value combined = apply_arith_op(cop, container->array_val.elements[idx], val);
                    free_value(&val);
                    val = combined;
                }
                Value stored = copy_value(val);
                free_value(&container->array_val.elements[idx]);
                container->array_val.elements[idx] = stored;
                return val;
            }
            if (container->type == TYPE_MAP) {
                char key[1024];
                int key_ok = key_from_value(idx_val, key, sizeof(key));
                free_value(&idx_val);
                if (!key_ok) {
                    report_error("Map key must be int, float, string, or bool");
                    free_value(&val);
                    return make_null();
                }
                if (cop != TOK_EOF) {
                    Value *current = map_get(container, key);
                    Value base = current ? borrow_value(*current) : make_null();
                    Value combined = apply_arith_op(cop, base, val);
                    free_value(&val);
                    val = combined;
                }
                map_set(container, key, val);
                free_value(&val);
                return make_null();
            }
            free_value(&idx_val);
            free_value(&val);
            report_error("Value is not indexable");
            return make_null();
        }

        case AST_FOREACH: {
            Value iter_src = eval_node(node->foreach_stmt.iterable);
            if (error_pending) { free_value(&iter_src); return make_int(0); }
            Value it = copy_value(iter_src);
            free_value(&iter_src);

            int n = 0;
            if (it.type == TYPE_ARRAY) n = it.array_val.size;
            else if (it.type == TYPE_MAP) n = it.map_val.size;
            else if (it.type == TYPE_STRING) n = (int)strlen(it.str_val);
            else report_error("for-in needs an array, map (iterates keys) or string");

            for (int i = 0; i < n; i++) {
                Value item;
                if (it.type == TYPE_ARRAY) {
                    item = copy_value(it.array_val.elements[i]);
                } else if (it.type == TYPE_MAP) {
                    item = make_string(it.map_val.keys[i]);
                } else {
                    char one[2] = { it.str_val[i], '\0' };
                    item = make_string(one);
                }
                set_var(node->foreach_stmt.var_name, item);
                free_value(&item);

                should_continue = false;
                eval_node(node->foreach_stmt.body);

                if (should_break) {
                    should_break = false;
                    break;
                }
                if (should_return || error_pending) break;
            }

            should_continue = false;
            free_value(&it);
            return make_int(0);
        }

        default:
            return make_int(0);
    }
}




