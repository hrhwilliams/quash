#ifndef __QUASH_TOKENIZER_H__
#define __QUASH_TOKENIZER_H__

#include "arrays.h"
#include "quash.h"

int tokenize(TokenDynamicArray *tokens, char *input);
int redirect(Token token);

#endif /* __QUASH_TOKENIZER_H__ */
