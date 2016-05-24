#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "compila.h"

#define TAMANHO_BYTES 1600

union _code{
	int a[400]; //1600 bytes
	unsigned char source[1600];
};


struct _mem {
	int index; // proximo index livre de source
	unsigned char* finalcode; // array totalmente preenchida com o código
	int* finalcode_int;
	Code* code;
};

typedef struct _stack Stack;
struct _stack {
	int height; //altura da pilha
	int alocheight; //altura alocada
	int locals[20]; //altura de cada varíavel local na pilha ( em relação a rbp )
};


/** Preenche o inicio e o fim de uma função SB **/
/* variaveis preenchidas:
 *  - prologo_inicio
 *  - prologo_fim
 */
static void preenche_prologo(unsigned char * inicio, unsigned char* fim)
{
	unsigned char in[4];
	unsigned char fi[2];
	in[0] = 0x55;
	in[1] = 0x48;
	in[2] = 0x89;
	in[3] = 0xE5;
	fi[0] = 0xC9;
	fi[1] = 0xC3;
	strcpy((char *)inicio, (char *)in);
	strcpy((char *)fim, (char *)fi);
}

/** Aloca o bloco de memória usado em compila() **/
/* variaveis alocadas:
 * - block
 */
static Memory* start()
{
	int i;
	Memory* strct;
	strct = (Memory *) malloc(sizeof(Memory));
	strct->index = 0;
	strct->finalcode = NULL;
	strct->code = (Code *) malloc(sizeof(Code));
	for(i = 0; i < TAMANHO_BYTES; i++ )
	{
		strct->code->source[i] = 0x90; //NOP
	}
	return strct;
}

/** Insere em block o array codigo **/
static void insere(Memory* block, unsigned char* codigo, int size)
{
	int i;
	if(block->index < 8000)
	{
		for(i = 0; i < size; i++)
		{
			block->code->source[block->index] = codigo[i];
			block->index++;
		}

	}
	else
	{
		printf("Estouro de block!! at insere()\n");
		exit(-1);
	}

}
/** finaliza o block **/
/*
 * Libera block->code
 * preenche block->finalcode
 * preenche block->finalcode_int
 *
 */
static void finaliza(Memory* block)
{
	int i;
	int* ptr;
	int code_intsize = ceil((block->index)/4.0);
	block->finalcode = (unsigned char*) malloc(sizeof(unsigned char) * block->index);
	block->finalcode_int = (int *) malloc(sizeof(int) * code_intsize);
	for(i = 0; i < block->index; i++)
	{
		block->finalcode[i] = block->code->source[i];
	}
	for(i = 0; i < code_intsize; i++)
	{
		ptr = (int*)(block->code->source + i*4);
		block->finalcode_int[i] = *(ptr);
	}
	free(block->code);
}

/** Inicializa uma pilha **/
static Stack* inicializa_pilha()
{
	Stack* p = (Stack *) malloc(sizeof(Stack));
	int i;
	for(i = 0; i < 20; i++)
	{
		p->locals[i] = 0;
	}
	p->height = 0;
	p->alocheight = 0;
	return p;
}
/** Gera o código de máquina de ret **/
static unsigned char* gera_ret(Stack* pilha, char var, int* idx, int* tamanho)
{
	unsigned char* machinecode;
	unsigned char* ptr;
	if(var == '$')
	{
		//movl $idx, %eax
		ptr = (unsigned char*)idx;
		machinecode = (unsigned char*) malloc(sizeof(unsigned char) * 5);
		machinecode[0] = 0xB8;
		*((int*) &machinecode[1]) = *idx;
		*tamanho += 5;
	}
	else if(var == 'p')
	{
		machinecode = (unsigned char*) malloc(sizeof(unsigned char) * 2);
		machinecode[0] = 0x89;
		if(*idx == 0)
			//movq %edi, %eax
			machinecode[1] = 0xF8;
		else if(*idx == 1)
			//movq %esi, %eax
			machinecode[1] = 0xF0;
		else if(*idx == 2)
			//movq %edx, %eax
			machinecode[1] = 0xD0;
		*tamanho += 2;
	}
	else if(var == 'v')
	{
		//movl -num(%rbp), %eax
		ptr = (unsigned char*) &pilha->locals[*idx];
		*ptr = *ptr & 0xFF;
		machinecode = (unsigned char*) malloc(sizeof(unsigned char) * 6);
		machinecode[0] = 0x8B;
		machinecode[1] = 0x45;
		*((int*) &machinecode[2]) = pilha->locals[*idx];
		*tamanho += 3;
	}

	return machinecode;
}

/** Gera código de atribuição **/
/** Utilizando %r13d para aritmética **/
static void gera_next(unsigned char* machinecode, Stack* pilha,int idx0, char op, char v2, int idx2, int* tamanho_string)
{
	int index = 0; // para inserir o epilogo
	switch(op){
		case '+':{
			if(v2 == 'v')
			{
				//addl -num(%rbp), %r13d
				machinecode[0] = 0x44;
				machinecode[1] = 0x03;
				machinecode[2] = 0x6D;
				*((int*) &machinecode[3]) = pilha->locals[idx2];
				*tamanho_string += 4;
				index = 4;

			}
			else if(v2 == '$')
			{
				//addl $num, %r13d
				machinecode[0] = 0x41;
				machinecode[2] = 0xC5;
				*((int*) &machinecode[3]) = idx2;
				if(idx2 < 128)
				{
					machinecode[1] = 0x83;
					*tamanho_string += 4;
					index = 4;
				}
				else
				{
					machinecode[1] = 0x81;
					*tamanho_string += 7;
					index = 7;
				}
			}

		}

	}

	//movl %r13d, -num(%rbp)
	machinecode[index] = 0x44;
	machinecode[index + 1] = 0x89;
	machinecode[index + 2] = 0x6D;
	*((int*) &machinecode[index + 3]) = pilha->locals[idx0];
	*tamanho_string += 4;

}
static unsigned char* gera_att(Stack* pilha, int idx0, char v1, int idx1, char op, char v2, int idx2, int* tamanho_string)
{
	unsigned char* ptrinicio;
	unsigned char* machinecode;
	machinecode = (unsigned char*) malloc(sizeof(unsigned char) * 100); //otimizar malloc()

	if((pilha->height % 8 == 0) || (pilha->height == 0))
		/* tira 16 da pilha*/
	{
		//subq $16, %rsp
		machinecode[0] = 0x48;
		machinecode[1] = 0x83;
		machinecode[2] = 0xEC;
		machinecode[3] = 0x10;
		*tamanho_string += 4;
		ptrinicio = machinecode;
		machinecode = machinecode+4;
	}
	//não é re-atribuição
	if(!pilha->locals[idx0])
	{
		pilha->alocheight -= 4;
		pilha->locals[idx0] = pilha->alocheight;
	}
	switch(v1){
		case '$':{
			// movl $idx1, %r13d
			machinecode[0] = 0x41;
			machinecode[1] = 0xBD;
			*((int*) &machinecode[2]) = idx1;
			*tamanho_string += 6;
			gera_next(machinecode+6, pilha, idx0, op, v2, idx2, tamanho_string);
		}
	}

	machinecode = ptrinicio;
	return machinecode;
}

/** Controle da geração de código de ret e atribuição **/
/** Desvia o fluxo para a função de geração correta **/
/** Insere em block o código de máquina gerado **/
static void gera(Stack* pilha, Memory* block, char caso, char var0, int idx0, char var1,int idx1,char op,char var2,
		int idx2)
{
	unsigned char* codetoi;
	int tamanho = 0;

	switch(caso){

		case 'r':{
			codetoi = gera_ret(pilha, var0, &idx0, &tamanho);
			insere(block, codetoi, tamanho);
			break;
		}
		case 'v':{
			codetoi = gera_att(pilha, idx0, var1, idx1, op, var2, idx2, &tamanho);
			insere(block, codetoi, tamanho);
			break;
		}
	}


}
/** Parser de SB, gera o código em um unsigned array e insere em block->code **/
 /*
  *
  */
static void error(const char *msg, int line) {
	fprintf(stderr, "erro %s na linha %d\n", msg, line);
	exit(EXIT_FAILURE);
}

void checkVar(char var, int idx, int line) {
	switch (var) {
	case 'v':
		if ((idx < 0) || (idx > 19))
			error("operando invalido", line);
		break;
	default:
		error("operando invalido", line);
	}
}

void checkVarP(char var, int idx, int line) {
	switch (var) {
	case 'v':
		if ((idx < 0) || (idx > 19))
			error("operando invalido", line);
		break;
	case 'p':
		if ((idx < 0) || (idx > 2))
			error("operando invalido", line);
		break;
	default:
		error("operando invalido", line);
	}
}
static void parser(Memory* block, FILE* myfp) {
	int line = 1;
	int c;
	Stack *pilha = inicializa_pilha();
	while ((c = fgetc(myfp)) != EOF) {
		switch (c) {
		case 'r': { /* retorno */
			int idx;
			char var;
			if (fscanf(myfp, "et %c%d", &var, &idx) != 2)
				error("comando invalido", line);
			if (var != '$')
				checkVarP(var, idx, line);

			//--//
			gera(pilha, block, 'r', var, idx, 0, 0, 0, 0, 0);

			printf("ret %c%d\n", var, idx);
			break;
		}
		case 'i': { /* if */
			int idx, n1, n2, n3;
			char var;
			if (fscanf(myfp, "f %c%d %d %d %d", &var, &idx, &n1, &n2, &n3) != 5)
				error("comando invalido", line);
			if (var != '$')
				checkVar(var, idx, line);
			printf("if %c%d %d %d %d\n", var, idx, n1, n2, n3);
			break;
		}
		case 'v': { /* atribuicao */
			int idx0, idx1, idx2;
			char var0 = c, var1, var2;
			char op;
			if (fscanf(myfp, "%d = %c%d %c %c%d", &idx0, &var1, &idx1, &op,
					&var2, &idx2) != 6)
				error("comando invalido", line);
			checkVar(var0, idx0, line);
			if (var1 != '$')
				checkVarP(var1, idx1, line);
			if (var2 != '$')
				checkVarP(var2, idx2, line);
			//--//
			gera(pilha, block, 'v', var0, idx0, var1, idx1, op, var2, idx2);
			printf("%c%d = %c%d %c %c%d\n", var0, idx0, var1, idx1, op, var2,
					idx2);
			break;
		}
		default:
			error("comando desconhecido", line);
		}
		line++;
		fscanf(myfp, " ");
	}
}

funcp compila(FILE *f)
{
	unsigned char prologo_inicio[4];
	unsigned char prologo_fim[2];
	Memory* block = start();
	preenche_prologo(prologo_inicio, prologo_fim);
	insere(block, prologo_inicio, 4);
	parser(block, f);
	insere(block, prologo_fim, 2);
	finaliza(block);
	return (funcp)block->finalcode;
}
