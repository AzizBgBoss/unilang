import argparse
import ast as py_ast
from pathlib import Path

from pycparser import c_ast, c_parser
import sys

DEFAULT_CODE = """
void main() {
    print("Hello world!");
    print("Say your name: ");
    char buf[8];
    input(buf);
    printn("Nice to meet you, ");
    printn(buf);
    print("!");
}
"""

TYPE_SIZES = {
    "uint8_t": 8,
    "unsigned char": 8,
    "char": 8,
    "uint16_t": 16,
    "short": 16,
    "uint32_t": 32,
    "int": 32,
    "unsigned int": 32,
}

BINOP_TO_UA = {"+": "addv", "-": "subv", "*": "mulv", "/": "divv", "%": "modv"}


class CompileError(Exception):
    pass


class UAGen(c_ast.NodeVisitor):
    def __init__(self):
        self.vars = {}
        self.next_addr = 0
        self.temp_top = 0
        self.output = []

    def compile(self, tree):
        main = self.find_main(tree)
        self.visit(main.body)
        return "\n".join(self.output)

    def find_main(self, tree):
        for node in tree.ext:
            if isinstance(node, c_ast.FuncDef) and node.decl.name == "main":
                return node
        raise CompileError("no main() function found")

    def alloc_scalar(self, name, typename):
        size = TYPE_SIZES.get(typename)
        if size is None:
            raise CompileError(f"unsupported type {typename!r}")
        addr = self.next_addr
        self.vars[name] = {"addr": addr, "size": size, "length": None}
        self.next_addr += size
        self.temp_top = self.next_addr
        return addr, size

    def alloc_array(self, name, typename, length):
        elem_size = TYPE_SIZES.get(typename)
        if elem_size is None:
            raise CompileError(f"unsupported array type {typename!r}")
        addr = self.next_addr
        self.vars[name] = {"addr": addr, "size": elem_size, "length": length}
        self.next_addr += elem_size * length
        self.temp_top = self.next_addr
        return addr, elem_size

    def push_temp(self, size):
        addr = self.temp_top
        self.temp_top += size
        return addr, size

    def clear_temps(self):
        self.temp_top = self.next_addr

    def type_name(self, type_node):
        if isinstance(type_node, c_ast.TypeDecl):
            return " ".join(type_node.type.names)
        raise CompileError(f"unsupported declaration type {type(type_node).__name__}")

    def int_constant(self, node):
        if node.type == "char":
            return ord(py_ast.literal_eval(node.value)), 8
        if node.type in {"int", "uint"}:
            return int(node.value, 0), 32
        raise CompileError(f"unsupported constant type {node.type!r}")

    def string_constant(self, node):
        return py_ast.literal_eval(node.value)

    def unilang_string_literal(self, text):
        if '"' in text:
            raise CompileError('string literals containing " are not supported by main.py')
        return f'"{text}"'

    def eval_expr(self, node):
        if isinstance(node, c_ast.Constant):
            value, size = self.int_constant(node)
            addr, size = self.push_temp(size)
            self.output.append(f"mem[{addr}:{size}]={value};")
            return addr, size

        if isinstance(node, c_ast.ID):
            var = self.vars.get(node.name)
            if var is None:
                raise CompileError(f"unknown variable {node.name!r}")
            if var["length"] is not None:
                raise CompileError(f"array {node.name!r} cannot be used as a scalar")
            return var["addr"], var["size"]

        if isinstance(node, c_ast.BinaryOp):
            op = BINOP_TO_UA.get(node.op)
            if op is None:
                raise CompileError(f"unsupported operator {node.op!r}")
            laddr, lsize = self.eval_expr(node.left)
            raddr, rsize = self.eval_expr(node.right)
            addr, size = self.push_temp(max(lsize, rsize))
            self.output.append(f"{op}({laddr}:{lsize}, {raddr}:{rsize}, {addr}:{size});")
            return addr, size

        if isinstance(node, c_ast.Cast):
            cast_type = self.type_name(node.to_type.type)
            cast_size = TYPE_SIZES.get(cast_type, 32)
            if isinstance(node.expr, c_ast.Constant):
                value, _ = self.int_constant(node.expr)
                addr, size = self.push_temp(cast_size)
                self.output.append(f"mem[{addr}:{size}]={value};")
                return addr, size
            addr, _ = self.eval_expr(node.expr)
            return addr, cast_size

        raise CompileError(f"unsupported expression {type(node).__name__}")

    def emit_expr_to(self, addr, size, node):
        if isinstance(node, c_ast.Constant):
            value, _ = self.int_constant(node)
            self.output.append(f"mem[{addr}:{size}]={value};")
            return

        if isinstance(node, c_ast.ID):
            src = self.vars.get(node.name)
            if src is None or src["length"] is not None:
                raise CompileError(f"cannot assign from {node.name!r}")
            self.output.append(f"add(0, {src['addr']}:{src['size']}, {addr}:{size});")
            return

        if isinstance(node, c_ast.BinaryOp):
            op = BINOP_TO_UA.get(node.op)
            if op is None:
                raise CompileError(f"unsupported operator {node.op!r}")
            laddr, lsize = self.eval_expr(node.left)
            raddr, rsize = self.eval_expr(node.right)
            self.output.append(f"{op}({laddr}:{lsize}, {raddr}:{rsize}, {addr}:{size});")
            return

        src_addr, src_size = self.eval_expr(node)
        self.output.append(f"add(0, {src_addr}:{src_size}, {addr}:{size});")

    def visit_Decl(self, node):
        if isinstance(node.type, c_ast.ArrayDecl):
            typename = self.type_name(node.type.type)
            if not isinstance(node.type.dim, c_ast.Constant):
                raise CompileError("array sizes must be constants")
            length = int(node.type.dim.value, 0)
            self.alloc_array(node.name, typename, length)
            if node.init is not None:
                raise CompileError("array initializers are not supported yet")
            return

        typename = self.type_name(node.type)
        addr, size = self.alloc_scalar(node.name, typename)
        if node.init is not None:
            self.emit_expr_to(addr, size, node.init)
            self.clear_temps()

    def visit_Assignment(self, node):
        if not isinstance(node.lvalue, c_ast.ID):
            raise CompileError("only simple variable assignment is supported")
        var = self.vars.get(node.lvalue.name)
        if var is None or var["length"] is not None:
            raise CompileError(f"cannot assign to {node.lvalue.name!r}")
        self.emit_expr_to(var["addr"], var["size"], node.rvalue)
        self.clear_temps()

    def visit_FuncCall(self, node):
        name = node.name.name
        args = node.args.exprs if node.args else []

        if name in {"print", "printn"}:
            self.emit_print(args, newline=name == "print")
            self.clear_temps()
            return

        if name == "input":
            self.emit_input(args)
            return

        raise CompileError(f"unsupported function call {name!r}")

    def emit_print(self, args, newline):
        if len(args) != 1:
            raise CompileError("print and printn take exactly one argument")
        arg = args[0]

        if isinstance(arg, c_ast.Constant) and arg.type in {"string", "char"}:
            text = self.string_constant(arg)
            command = "print" if newline else "printn"
            self.output.append(f"{command}({self.unilang_string_literal(text)});")
            return

        if isinstance(arg, c_ast.ID):
            var = self.vars.get(arg.name)
            if var is None:
                raise CompileError(f"unknown variable {arg.name!r}")
            if var["length"] is None:
                self.output.append(f"out({var['addr']}:{var['size']});")
                if newline:
                    self.output.append('print("");')
            else:
                command = "printv" if newline else "printvn"
                self.output.append(f"{command}({var['length']}, {var['addr']}:{var['size']});")
            return

        if isinstance(arg, c_ast.Cast):
            addr, size = self.eval_expr(arg)
            cast_type = self.type_name(arg.to_type.type)
            command = "outc" if cast_type == "char" else "out"
            self.output.append(f"{command}({addr}:{size});")
            if newline:
                self.output.append('print("");')
            return

        addr, size = self.eval_expr(arg)
        self.output.append(f"out({addr}:{size});")
        if newline:
            self.output.append('print("");')

    def emit_input(self, args):
        if len(args) != 1 or not isinstance(args[0], c_ast.ID):
            raise CompileError("input takes one array variable")
        var = self.vars.get(args[0].name)
        if var is None or var["length"] is None:
            raise CompileError("input target must be an array")
        self.output.append(f"input({var['length']}, {var['addr']}:{var['size']});")


def compile_code(source):
    parser = c_parser.CParser()
    tree = parser.parse(source, filename="<source>")
    return UAGen().compile(tree)


def main():
    argparser = argparse.ArgumentParser(description="Compile C-like unilang source.")
    argparser.add_argument("source", nargs="?", help="source file to compile")
    argparser.add_argument("-o", "--output", help="output file to write")

    args = argparser.parse_args()

    source = Path(args.source).read_text() if args.source else DEFAULT_CODE

    output = compile_code(source)

    if args.output:
        Path(args.output).write_text(output)
    else:
        print(output)

if __name__ == "__main__":
    main()
