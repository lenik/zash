#ifndef ZASH_BUILTINS_HPP
#define ZASH_BUILTINS_HPP

#include "../zash_interpreter.hpp"

namespace os::zash {

void register_default_builtins(Interpreter &interp);
void register_default_aliases(Interpreter &interp);

}

#endif
