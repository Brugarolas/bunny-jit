
#include "bjit.h"

int main()
{

    bjit::Module    module;
    bjit::Proc      proc(0, "ii");

    proc.iret(proc.iadd(proc.env[0], proc.env[1]));

    int i = module.compile(proc);

    assert(module.load());

    auto ptr = module.getPointer<int(int,int)>(i);

    printf(" 2 + 5 = %d\n", ptr(2, 5));

    assert(ptr(2,5) == 7);

    return 0;
}
