from pycparser import c_parser, c_ast

code = """
void main() {
    unsigned int a = 5;
    char b = 7 + a + 1;
    b = b / 2;
    print(b);
    print('t');
    print("nice!!");
    print((char) 5);
}
"""

parser = c_parser.CParser()
ast = parser.parse(code, filename='<none>')
ast.show()

TYPE_SIZES = {
    "uint8_t": 8, "unsigned char": 8, "char": 8,
    "uint16_t": 16, "short": 16,
    "uint32_t": 32, "int": 32, "unsigned int": 32,
}

BINOP_TO_UA = {"+": "add", "-": "sub", "*": "mul", "/": "div", "%": "mod"}

class UAGen(c_ast.NodeVisitor):
    def __init__(self):
        self.vars = {}          # name -> (addr, size)
        self.next_addr = 0      # bump allocator for real vars
        self.temp_top = 0       # stack pointer for temps (grows/shrinks)
        self.output = []

    # --- real variable allocation (permanent, never freed) ---
    def alloc(self, name, typename):
        size = TYPE_SIZES.get(typename, 32)
        addr = self.next_addr
        self.vars[name] = (addr, size)
        self.next_addr += size
        self.temp_top = self.next_addr   # temps start above all real vars
        self.output.append(f"# alloc {name} ({typename}, {size} bit) at addr {addr}")
        return addr, size

    # --- temp stack: push before use, pop right after consumed ---
    def push_temp(self, size):
        addr = self.temp_top
        self.temp_top += size
        return addr, size

    def pop_temp(self, size):
        self.temp_top -= size

    def eval_expr(self, node):
        """Evaluate an expression node, return (addr, size) of its result.
        Any temp this creates is the caller's responsibility to pop once consumed."""
        if isinstance(node, c_ast.Constant):
            addr, size = self.push_temp(32)
            self.output.append(f"mem[{addr}:{size}]={node.value};")
            return addr, size
        elif isinstance(node, c_ast.ID):
            return self.vars[node.name]   # existing var, no temp, nothing to free
        elif isinstance(node, c_ast.BinaryOp):
            op = BINOP_TO_UA.get(node.op)
            if op is None:
                raise NotImplementedError(f"unsupported operator {node.op}")

            laddr, lsize = self.eval_expr(node.left)
            left_is_temp = not isinstance(node.left, c_ast.ID)

            raddr, rsize = self.eval_expr(node.right)
            right_is_temp = not isinstance(node.right, c_ast.ID)

            result_size = max(lsize, rsize)
            rtaddr, rtsize = self.push_temp(result_size)
            self.output.append(f"{op}v({laddr}:{lsize}, {raddr}:{rsize}, {rtaddr}:{rtsize});")

            # free right first (it's on top of the stack), then left
            if right_is_temp:
                self.pop_temp(rsize)
            if left_is_temp:
                self.pop_temp(lsize)

            # result temp was pushed after both operands were freed conceptually,
            # but since we computed its address before freeing, move it down
            # to sit right after the freed space (stack now points past operands)
            # simplest correct fix: re-push at current (now-lower) temp_top
            final_addr = self.temp_top
            if final_addr != rtaddr:
                self.output.append(f"setmem({final_addr}:{rtsize}, {rtaddr}:{rtsize});")
            self.temp_top += rtsize
            return final_addr, rtsize
        elif isinstance(node, c_ast.FuncCall):
            if node.name.name == "print":
                return self.eval_expr(node.args.exprs[0])
        else:
            raise NotImplementedError(f"unsupported expr node {type(node).__name__}")

    def emit_store(self, addr, size, node):
        saddr, ssize = self.eval_expr(node)
        if (saddr, ssize) != (addr, size):
            self.output.append(f"setmem({addr}:{size}, {saddr}:{ssize});")
        # result temp (if any) is no longer needed after the store
        if saddr >= self.next_addr:   # it was a temp, not a real var
            self.pop_temp(ssize)

    def visit_Decl(self, node):
        if isinstance(node.type, c_ast.TypeDecl):
            typename = " ".join(node.type.type.names)
            addr, size = self.alloc(node.name, typename)
            if node.init is not None:
                self.emit_store(addr, size, node.init)
        self.generic_visit(node)

    def visit_Assignment(self, node):
        target = node.lvalue.name
        addr, size = self.vars[target]
        self.emit_store(addr, size, node.rvalue)

    def visit_FuncCall(self, node):
        if node.name.name == "print":
            arg = node.args.exprs[0]
            if isinstance(arg, c_ast.ID):
                addr, size = self.eval_expr(arg)
                self.output.append(f"out({addr}:{size});")
                if addr >= self.next_addr:   # free if it was a temp
                    self.pop_temp(size)
            elif isinstance(arg, c_ast.Constant):
                if arg.type == "char" or arg.type == "string":
                    text = arg.value[1:-1]
                else:
                    text = str(arg.value)
                self.output.append(f"print(\"{text}\");")
            elif isinstance(arg, c_ast.Cast):
                cast_type = arg.to_type.type.type.names[0]
                cast_size = TYPE_SIZES.get(cast_type, 32)
                if isinstance(arg.expr, c_ast.Constant):
                    addr, size = self.push_temp(cast_size)
                    self.output.append(f"mem[{addr}:{size}]={arg.expr.value};")
                else:
                    addr, size = self.eval_expr(arg.expr)
                if cast_type == "char":
                    self.output.append(f"outc({addr}:{cast_size});")
                else:
                    self.output.append(f"out({addr}:{cast_size});")
                if addr >= self.next_addr:
                    self.pop_temp(size)

gen = UAGen()
gen.visit(ast)
print("\n".join(gen.output))