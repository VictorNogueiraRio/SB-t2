#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "compila.h"

#define TAMANHO_BYTES 1600

typedef union _code Code;
typedef struct _mem Memory;

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

typedef struct _codelines Lines;
struct _codelines{
	unsigned char linha[50]; //index da tradução de cada linha em source
	unsigned char indexif[50]; //index de buraco do if
	unsigned char linhaif[50]; //linha em que se localiza o if
	unsigned char argsif[50]; // args de if (de 3 em 3)
	unsigned char indexret[50]; //index de buraco de ret
	unsigned char gerouif; //bool se gerou algum if
	int idxlinhaif;
	int idxargsif;
	int idxret;
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
static void insere(Memory* block, unsigned char* codigo, int size, Lines* linhas, int linha_atual)
{
	int i;
	if(block->index < TAMANHO_BYTES)
	{
		if(linha_atual > 0)
			linhas->linha[linha_atual] = block->index;
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
static unsigned char* gera_ret(Stack* pilha, Lines* linhas, char var, int* idx, int* tamanho)
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
		//jmp
		machinecode[5] = 0xEB;
		*tamanho += 7;
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
		//jmp
		machinecode[2] = 0xEB;
		*tamanho += 4;
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
		//jmp
		machinecode[3] = 0xEB;
		*tamanho += 5;
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
				 else if(v2 == 'p')
				 {

					 //addl regde'p', %r13d
					 machinecode[0] = 0x41;
					 machinecode[1] = 0x01;
					 if(idx2 == 0)
					 {
						 //addl %edi, %r13d
						 machinecode[2] = 0xFD;
					 }
					 else if(idx2 == 1)
					 {
						 //addl %esi, %r13d
						 machinecode[2] = 0xF5;
					 }
					 else if(idx2 == 2)
					 {
						 //addl %edx, %r13d
						 machinecode[2] = 0xD5;
					 }
					 *tamanho_string += 3;
					 index = 3;

				 }
				 break;
			 }
		case '-':
		{
			if(v2 == 'v')
			{
				//subl -num(%rbp), %r13d
				machinecode[0] = 0x44;
				machinecode[1] = 0x2B;
				machinecode[2] = 0x6D;
				*((int*) &machinecode[3]) = pilha->locals[idx2];
				*tamanho_string += 4;
				index = 4;
			}
			else if(v2 == 'p')
			{
				//subl regde'p', %r13d
				machinecode[0] = 0x41;
				machinecode[1] = 0x29;
				if(idx2 == 0)
				{
					//subl %edi, %r13d
					machinecode[2] = 0xFD;
				}
				else if(idx2 == 1)
				{
					//subl %esi, %r13d
					machinecode[2] = 0xF5;
				}
				else if(idx2 == 2)
				{
					//subl %edx, %r13d
					machinecode[2] = 0xD5;
				}
				*tamanho_string += 3;
				index = 3;

			}
			else if(v2 == '$')
			{
				//subl $num, %r13d
				machinecode[0] = 0x41;
				machinecode[2] = 0xED;
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

			break;
		}
		case '*':
		{

			if(v2 == 'v')
			{
				//imull -num(%rbp), %r13d
				machinecode[0] = 0x44;
				machinecode[1] = 0x0F;
				machinecode[2] = 0xAF;
				machinecode[3] = 0x6D;
				*((int *) &machinecode[4]) = pilha->locals[idx2];
				*tamanho_string += 5;
				index = 5;
			}
			else if(v2 == 'p')
			{
				//imull regde'p', %r13d
				machinecode[0] = 0x44;
				machinecode[1] = 0x0F;
				machinecode[2] = 0xAF;
				if(idx2 == 0)
				{
					//imull %edi, %r13d
					machinecode[3] = 0xEF;
				}
				else if(idx2 == 1)
				{
					//imull %esi, %r13d
					machinecode[3] = 0xEE;
				}
				else if(idx2 == 2)
				{
					//imull %edx, %r13d
					machinecode[3] = 0xEA;
				}
				*tamanho_string += 4;
				index = 4;

			}
			else if(v2 == '$')
			{
				//imull $num, %r13d
				machinecode[0] = 0x45;
				machinecode[2] = 0xED;
				*((int*) &machinecode[3]) = idx2;
				if(idx2 < 128)
				{
					machinecode[1] = 0x6B;
					*tamanho_string += 4;
					index = 4;
				}
				else
				{
					machinecode[1] = 0x69;
					*tamanho_string += 7;
					index = 7;
				}

			}
			break;
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
	short sub = 0;
	unsigned char* ptrinicio;
	unsigned char* machinecode;
	machinecode = (unsigned char*) malloc(sizeof(unsigned char) * 100); //otimizar malloc()

	if((pilha->height % 16 == 0) || (pilha->height == 0))
		/* tira 16 da pilha*/
	{
		//subq $16, %rsp
		sub = 1;
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
		pilha->height += 4;
	}
	switch(v1){
		case '$':{
				 // movl $idx1, %r13d
				 machinecode[0] = 0x41;
				 machinecode[1] = 0xBD;
				 *((int*) &machinecode[2]) = idx1;
				 *tamanho_string += 6;
				 gera_next(machinecode+6, pilha, idx0, op, v2, idx2, tamanho_string);
				 break;
			 }
		case 'p':{
				 //movl regde'p', %r13d
				 machinecode[0] = 0x41;
				 machinecode[1] = 0x89;
				 if(idx1 == 0)
					 //movl %edi, %r13d
					 machinecode[2] = 0xFD;
				 else if(idx1 == 1)
					 //movl %esi, %r13d
					 machinecode[2] = 0xF5;
				 else if(idx1 == 2)
					 //movl %edx, %r13d
					 machinecode[2] = 0xD5;
				 *tamanho_string += 3;
				 gera_next(machinecode+3, pilha, idx0, op, v2, idx2, tamanho_string);
				 break;
			 }
		case 'v':{
			//movl -num(%rbp), %r13d
			machinecode[0] = 0x44;
			machinecode[1] = 0x8B;
			machinecode[2] = 0x6D;
			*((int *) &machinecode[3]) = pilha->locals[idx1];
			*tamanho_string += 4;
			gera_next(machinecode+4, pilha, idx0, op, v2, idx2, tamanho_string);
			break;
		}
	}

	if(sub)
		machinecode = ptrinicio;

	return machinecode;
}

/** Gera a o prólogo da instrução if **/
static unsigned char* gera_if(Stack* pilha, int idx0, int* tamanho_string)
{
	unsigned char* machinecode = (unsigned char*) malloc(sizeof(unsigned char) * 20); //otimizar malloc()

	//movl -num(%rbp), %r13d
	machinecode[0] = 0x44;
	machinecode[1] = 0x8B;
	machinecode[2] = 0x6D;
	*((int*) &machinecode[3]) = pilha->locals[idx0];
	//cmpl $0, %r13d
	machinecode[4] = 0x41;
	machinecode[5] = 0x83;
	machinecode[6] = 0xFD;
	machinecode[7] = 0x00;

	*tamanho_string += 8;

	return machinecode;
}
/** Controle da geração de código de ret, atribuição e if **/
/** Desvia o fluxo para a função de geração correta **/
/** Insere em block o código de máquina gerado **/
static void gera(Stack* pilha, Memory* block, char caso, char var0, int idx0, char var1,int idx1,char op,char var2,
		int idx2, int idx3, int linha_atual, Lines* linhas)
{
	unsigned char* codetoi;
	int tamanho = 0;

	switch(caso){

		case 'r':{
				 codetoi = gera_ret(pilha,linhas, var0, &idx0, &tamanho);
				 linhas->indexret[linhas->idxret] = (block->index + tamanho) - 1;
				 linhas->idxret += 1;
				 insere(block, codetoi, tamanho, linhas, linha_atual);
				 free(codetoi);
				 break;
			 }
		case 'v':{
				 codetoi = gera_att(pilha, idx0, var1, idx1, op, var2, idx2, &tamanho);
				 insere(block, codetoi, tamanho, linhas, linha_atual);
				 free(codetoi);
				 break;
			 }
		case 'i':{
			linhas->linhaif[linhas->idxlinhaif] = linha_atual;
			linhas->argsif[linhas->idxargsif] = idx1;
			linhas->argsif[linhas->idxargsif + 1] = idx2;
			linhas->argsif[linhas->idxargsif + 2] = idx3;
			linhas->idxargsif += 3;
			codetoi = gera_if(pilha, idx0, &tamanho);
			insere(block, codetoi, tamanho, linhas, linha_atual);
			//marca index pra preencher
			linhas->indexif[linhas->idxlinhaif] = block->index;
			linhas->idxlinhaif += 1;
			block->index += 6;
			free(codetoi);
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
static void parser(Memory* block, FILE* myfp, Lines* linhas) {
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
					  gera(pilha, block, 'r', var, idx, 0, 0, 0, 0, 0, 0, line -1, linhas);

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
					  linhas->gerouif = 1;
					  gera(pilha, block, 'i', 0, idx, 0, n1, 0, 0, n2, n3, line - 1, linhas);
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
					  gera(pilha, block, 'v', var0, idx0, var1, idx1, op, var2, idx2, 0, line -1, linhas);
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

void debug(Memory* block)
{
	int i;
	for(i = 0; i < block->index; i++)
	{
		printf("%d : %x\n", i, block->code->source[i]);
	}
}

/** Preenche os buracos de if e de ret **/
static void preenche_resto(Lines* linhas, Memory* block)
{
	unsigned char dsl, dse, dsg, dsr;
	int i, countif, k = 0;
	//quantos if gerados
	countif = linhas->idxargsif / 3;
	for(i = 0; countif > 0; countif--, k++, i+=3)
	{
		dsl = linhas->linha[(linhas->argsif[i] - 1)] - ((linhas->linha[linhas->linhaif[k]]) + 8);
		dse = linhas->linha[(linhas->argsif[i + 1] - 1)] - ((linhas->linha[linhas->linhaif[k]] + 2) + 8);
		dsg = linhas->linha[(linhas->argsif[i + 2] - 1)] - ((linhas->linha[linhas->linhaif[k]] + 4) + 8);

		// jl
		block->code->source[linhas->indexif[k]] = 0x7C;
		if(dsl < 0)
		{
			dsl--;
			block->code->source[linhas->indexif[k] + 1] = dsl;
		}
		else if(dsl > 0)
		{
			dsl -=2;
			block->code->source[linhas->indexif[k] + 1] = dsl;
		}
		// je
		block->code->source[linhas->indexif[k] + 2] = 0x74;
		if(dse < 0)
		{
			dse--;
			block->code->source[linhas->indexif[k] + 3] = dse;
		}
		else if(dse > 0)
		{
			dse -=2;
			block->code->source[linhas->indexif[k] + 3] = dse;
		}
		// jg
		block->code->source[linhas->indexif[k] + 4] = 0x7F;
		if(dsg < 0)
		{
			dsg--;
			block->code->source[linhas->indexif[k] + 5] = dsg;
		}
		else if(dsg > 0)
		{
			dsg -=2;
			block->code->source[linhas->indexif[k] + 5] = dsg;
		}

	}
	for(i = 0; i < linhas->idxret; i++)
	{
		dsr = (block->index - linhas->indexret[i]) - 1;
		block->code->source[linhas->indexret[i]] = dsr;
	}



}

funcp compila(FILE *f)
{
	int i;
	unsigned char prologo_inicio[4];
	unsigned char prologo_fim[2];
	Lines linhas;
	for(i = 0; i < 50; i++)
	{
		linhas.linha[i] = 0;
		linhas.indexif[i] = 0;
		linhas.linhaif[i] = 0;
		linhas.argsif[i] = -1;
	}
	linhas.gerouif = 0;
	linhas.idxlinhaif = 0;
	linhas.idxargsif = 0;
	linhas.idxret = 0;
	Memory* block = start();
	preenche_prologo(prologo_inicio, prologo_fim);
	insere(block, prologo_inicio, 4, &linhas, -1);
	parser(block, f, &linhas);
	preenche_resto(&linhas, block);
	insere(block, prologo_fim, 2, &linhas, -1);
	debug(block);
	finaliza(block);
	return (funcp)block->finalcode;
}
