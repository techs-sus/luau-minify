#include "block.hpp"

DoBlock::DoBlock() : Block(DoBlock::ClassIndex()) {}

SingleConditionBlock::SingleConditionBlock(SingleConditionBlock::Type type,
                                           Luau::AstExpr *condition)
    : Block(SingleConditionBlock::ClassIndex()), type(type),
      condition(condition) {}

ForBlock::ForBlock(Luau::AstLocal *variable, Luau::AstExpr *from,
                   Luau::AstExpr *to, Luau::AstExpr *step)
    : Block(ForBlock::ClassIndex()), variable(variable), from(from), to(to),
      step(step) {}

ForInBlock::ForInBlock(const Luau::AstArray<Luau::AstLocal *> *vars,
                       const Luau::AstArray<Luau::AstExpr *> *values)
    : Block(ForInBlock::ClassIndex()), vars(vars), values(values) {}

FunctionBlock::FunctionBlock(char *name, bool variadic,
                             Luau::AstArray<Luau::AstLocal *> *arguments)
    : Block(FunctionBlock::ClassIndex()), name(name), variadic(variadic),
      arguments(arguments) {}

IfStatementBlock::IfStatementBlock()
    : Block(IfStatementBlock::ClassIndex()), condition(nullptr),
      thenBody(nullptr), elseBody(nullptr), elseifs({}) {}

IfBlock::IfBlock() : Block(IfBlock::ClassIndex()), type(IfBlock::Type::Then) {}

RootBlock::RootBlock() : Block(RootBlock::ClassIndex()) {}
