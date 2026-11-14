#include "eval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../builtin/builtin.h"
#include "../builtin/extended_builtin.h"
#include "../builtin/security_builtin.h"
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

    eval_node(func->body);

    Value result = return_value;
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
    return_value = make_null();   /* a callback without `return` yields null */

    eval_node(func->body);

    Value result = return_value;
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
                char result[MAX_STRING_LEN];
                /* Non-string operands are rendered like print() would show
                 * them (true/false/null, arrays, maps), not as raw numbers. */
                if (left.type == TYPE_STRING) {
                    strncpy(result, left.str_val, MAX_STRING_LEN - 1);
                    result[MAX_STRING_LEN - 1] = '\0';
                } else {
                    value_to_display_string(left, result, MAX_STRING_LEN);
                }

                int len = strlen(result);
                if (right.type == TYPE_STRING) {
                    strncat(result, right.str_val, MAX_STRING_LEN - len - 1);
                } else {
                    char temp[2048];
                    value_to_display_string(right, temp, sizeof(temp));
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

        if (base->type == TYPE_ARRAY) {
            int idx = to_int(idx_val);
            if (idx < 0 || idx >= base->array_val.size) {
                report_error("Array index out of bounds");
                return NULL;
            }
            return &base->array_val.elements[idx];
        }
        if (base->type == TYPE_MAP) {
            char key[1024];
            if (!key_from_value(idx_val, key, sizeof(key))) return NULL;
            return map_get(base, key);
        }
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
            return *v;
        }

        case AST_BINARY_OP: {
            Value left = eval_node(node->binary.left);

            /* and / or short-circuit, like JavaScript and Python */
            if (node->binary.op == TOK_AND && !to_bool(left)) return left;
            if (node->binary.op == TOK_OR && to_bool(left)) return left;

            Value right = eval_node(node->binary.right);

            switch (node->binary.op) {
                case TOK_PLUS:
                case TOK_MINUS:
                case TOK_STAR:
                case TOK_SLASH:
                    return apply_arith_op(node->binary.op, left, right);

                case TOK_PERCENT:
                    if (to_int(right) == 0) {
                        report_error("Modulo by zero");
                        return make_int(0);
                    }
                    return make_int(to_int(left) % to_int(right));

                case TOK_EQ:
                    if (left.type == TYPE_NULL || right.type == TYPE_NULL) {
                        return make_bool(left.type == TYPE_NULL && right.type == TYPE_NULL);
                    }
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        return make_bool(strcmp(left.str_val, right.str_val) == 0);
                    }
                    return make_bool(to_float(left) == to_float(right));
                case TOK_NEQ:
                    if (left.type == TYPE_NULL || right.type == TYPE_NULL) {
                        return make_bool(!(left.type == TYPE_NULL && right.type == TYPE_NULL));
                    }
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        return make_bool(strcmp(left.str_val, right.str_val) != 0);
                    }
                    return make_bool(to_float(left) != to_float(right));
                case TOK_LT:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        return make_bool(strcmp(left.str_val, right.str_val) < 0);
                    }
                    return make_bool(to_float(left) < to_float(right));
                case TOK_GT:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        return make_bool(strcmp(left.str_val, right.str_val) > 0);
                    }
                    return make_bool(to_float(left) > to_float(right));
                case TOK_LTE:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        return make_bool(strcmp(left.str_val, right.str_val) <= 0);
                    }
                    return make_bool(to_float(left) <= to_float(right));
                case TOK_GTE:
                    if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
                        return make_bool(strcmp(left.str_val, right.str_val) >= 0);
                    }
                    return make_bool(to_float(left) >= to_float(right));
                case TOK_IN: {
                    if (right.type == TYPE_ARRAY) {
                        for (int i = 0; i < right.array_val.size; i++) {
                            if (values_equal(left, right.array_val.elements[i])) return make_bool(1);
                        }
                        return make_bool(0);
                    }
                    if (right.type == TYPE_MAP) {
                        char key[1024];
                        if (!key_from_value(left, key, sizeof(key))) return make_bool(0);
                        return make_bool(map_get(&right, key) != NULL);
                    }
                    if (right.type == TYPE_STRING && left.type == TYPE_STRING) {
                        return make_bool(strstr(right.str_val, left.str_val) != NULL);
                    }
                    report_error("'in' needs an array, map or string on the right");
                    return make_bool(0);
                }
                case TOK_AND:   /* left was truthy -> result is the right operand */
                case TOK_OR:    /* left was falsy  -> result is the right operand */
                    return right;

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
                    return make_bool(!to_bool(operand));
                default:
                    return make_int(0);
            }
        }

        case AST_ASSIGN: {
            Value val = eval_node(node->assign.value);

            if (node->assign.compound_op != TOK_EOF) {
                Value *current = get_var(node->assign.var_name);
                if (!current) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Undefined variable: %s", node->assign.var_name);
                    report_error(msg);
                    return make_null();
                }
                val = apply_arith_op(node->assign.compound_op, *current, val);
            }

            if (error_pending) return make_null();   /* failed inside try: leave the variable alone */

            /* a = a[0]  /  a = a.child: deep-copy first so freeing the old
             * value of `a` can't invalidate what we're about to store. */
            if (val.type == TYPE_ARRAY || val.type == TYPE_MAP) {
                val = copy_value(val);
            }
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
                    set_var(node->try_stmt.var_name, make_string(message));
                }
                eval_node(node->try_stmt.catch_block);
            }
            return make_int(0);
        }

        case AST_THROW: {
            Value v = eval_node(node->throw_stmt.value);
            if (error_pending) return make_int(0);
            char message[512];
            value_to_display_string(v, message, sizeof(message));
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

            Value *arg_values = (Value*)malloc(sizeof(Value) * node->call.arg_count);

            bool is_mutating_array_call =
                (strcmp(node->call.func_name, "push") == 0 || strcmp(node->call.func_name, "pop") == 0) &&
                node->call.arg_count > 0 && node->call.args[0]->type == AST_VARIABLE;

            for (int i = 0; i < node->call.arg_count; i++) {
                if (i == 0 && is_mutating_array_call) {
                    /* push()/pop() realloc/free the array buffer in place, so
                     * they must operate on an independent copy rather than
                     * the pointer shared with the stored variable. */
                    Value *src = get_var(node->call.args[0]->var_name);
                    arg_values[0] = src ? copy_value(*src) : make_int(0);
                } else {
                    arg_values[i] = eval_node(node->call.args[i]);
                }
            }

            if (error_pending) {      /* an argument failed inside try: don't run the call */
                free(arg_values);
                return make_null();
            }

            if (!via_variable && is_builtin_function(node->call.func_name)) {
                Value result = call_builtin_function(node->call.func_name, arg_values, node->call.arg_count);

                if (is_mutating_array_call) {
                    set_var(node->call.args[0]->var_name, arg_values[0]);
                    free_value(&arg_values[0]);
                }

                free(arg_values);
                return result;
            }

            if (!via_variable && is_extended_builtin(node->call.func_name)) {
                Value result = call_extended_builtin(node->call.func_name, arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            if (!via_variable && is_security_builtin(node->call.func_name)) {
                Value result = call_security_builtin(node->call.func_name, arg_values, node->call.arg_count);
                free(arg_values);
                return result;
            }

            Function *func = find_function(target_name);

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
                declare_local_var(func->params[i], arg_values[i]);
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
                if (should_break || should_continue || should_return || error_pending) break;
                eval_node(node->block.statements[i]);
            }
            return make_int(0);
        }

        case AST_IF: {
            Value condition = eval_node(node->if_stmt.condition);
            if (error_pending) return make_int(0);
            if (to_bool(condition)) {
                eval_node(node->if_stmt.then_block);
            } else if (node->if_stmt.else_block) {
                eval_node(node->if_stmt.else_block);
            }
            return make_int(0);
        }

        case AST_WHILE: {
            while (1) {
                Value condition = eval_node(node->while_stmt.condition);
                if (error_pending || !to_bool(condition)) break;

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
            if (error_pending) return make_null();
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
            if (error_pending) return make_int(0);   /* an error inside try: don't print */
            print_value(val);
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
                arr.array_val.elements[i] = eval_node(node->array_literal.elements[i]);
            }
            return arr;
        }

        case AST_MAP_LITERAL: {
            Value m = make_map();
            for (int i = 0; i < node->map_literal.count; i++) {
                Value k = eval_node(node->map_literal.keys[i]);
                char key[1024];
                if (!key_from_value(k, key, sizeof(key))) {
                    report_error("Map key must be int, float, string, or bool");
                    continue;
                }
                Value v = eval_node(node->map_literal.values[i]);
                map_set(&m, key, v);
            }
            return m;
        }

        case AST_INDEX: {
            Value base = eval_node(node->index.object);
            Value idx_val = eval_node(node->index.index);

            if (base.type == TYPE_ARRAY) {
                int idx = to_int(idx_val);
                if (idx < 0 || idx >= base.array_val.size) {
                    report_error("Array index out of bounds");
                    return make_null();
                }
                return base.array_val.elements[idx];
            }
            if (base.type == TYPE_MAP) {
                char key[1024];
                if (!key_from_value(idx_val, key, sizeof(key))) {
                    report_error("Map key must be int, float, string, or bool");
                    return make_null();
                }
                Value *found = map_get(&base, key);
                return found ? *found : make_null();
            }
            if (base.type == TYPE_STRING) {
                int idx = to_int(idx_val);
                int len = (int)strlen(base.str_val);
                if (idx < 0 || idx >= len) {
                    report_error("String index out of bounds");
                    return make_null();
                }
                char one[2] = { base.str_val[idx], '\0' };
                return make_string(one);
            }
            report_error("Value is not indexable");
            return make_null();
        }

        case AST_INDEX_ASSIGN: {
            ASTNode *t = node->index_assign.target;
            Value val = eval_node(node->index_assign.value);
            if (error_pending) return make_null();
            Value *container = lvalue_ptr(t->index.object);
            if (!container) {
                report_error("Cannot assign: the container is undefined or missing");
                return make_null();
            }
            Value idx_val = eval_node(t->index.index);
            NaCTokenType cop = node->index_assign.compound_op;

            if (container->type == TYPE_ARRAY) {
                int idx = to_int(idx_val);
                if (idx < 0 || idx >= container->array_val.size) {
                    report_error("Array index out of bounds");
                    return make_null();
                }
                if (cop != TOK_EOF) {
                    val = apply_arith_op(cop, container->array_val.elements[idx], val);
                }
                Value stored = copy_value(val);
                free_value(&container->array_val.elements[idx]);
                container->array_val.elements[idx] = stored;
                return val;
            }
            if (container->type == TYPE_MAP) {
                char key[1024];
                if (!key_from_value(idx_val, key, sizeof(key))) {
                    report_error("Map key must be int, float, string, or bool");
                    return make_null();
                }
                if (cop != TOK_EOF) {
                    Value *current = map_get(container, key);
                    Value base = current ? *current : make_null();
                    val = apply_arith_op(cop, base, val);
                }
                map_set(container, key, val);
                return val;
            }
            report_error("Value is not indexable");
            return make_null();
        }

        case AST_FOREACH: {
            Value it = copy_value(eval_node(node->foreach_stmt.iterable));
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




