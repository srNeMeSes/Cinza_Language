# Makefile para o Compilador da Linguagem Cinza
# C++20, otimização -O3, warnings habilitados
# No PowerShell (MinGW) o comando é `mingw32-make`; no MSYS2 é `make`.
# `make debug` (ASan/UBSan) só funciona no Windows pelo MSYS2 CLANG64.

CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -pedantic
DEPFLAGS = -MMD -MP
TARGET = cinza
SOURCES = main.cpp lexer.cpp parser.cpp ast.cpp semantic.cpp executor.cpp operacoes.cpp natives.cpp stdlib.cpp module_loader.cpp \
          cvm/compiler.cpp cvm/vm.cpp cvm/disasm.cpp
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

# No MSYS2 CLANG64 não existe g++, só clang++ (é o ambiente com ASan no Windows)
ifeq ($(MSYSTEM),CLANG64)
    CXX = clang++
endif

# O binário ganha .exe em qualquer Windows (PowerShell, MSYS2).
# A6: a pilha padrão do Windows (1 MB) só comporta ~1000 chamadas aninhadas
# da Cinza; com 64 MB o limite de 2000 do executor (StackOverflowError) é
# atingido antes de a pilha nativa estourar, com folga para corpos aninhados.
ifneq ($(OS)$(MSYSTEM),)
    EXE = .exe
    LDFLAGS = -Wl,--stack,67108864
else
    EXE =
    LDFLAGS =
endif

# Diferenças de shell: sem sh no PATH (PowerShell/cmd) o make fica com o
# valor padrão "sh.exe" e roda as receitas no cmd.exe
ifeq ($(SHELL),sh.exe)
    RM = del /Q /F
    PYTHON ?= python
    # o del do cmd entende '/' como opção: caminhos precisam de '\'
    fixpath = $(subst /,\,$1)
    # O cmd não interpreta códigos ANSI
    RED =
    GREEN =
    YELLOW =
    NC =
else
    RM = rm -f
    PYTHON ?= python3
    fixpath = $1
    RED = \033[0;31m
    GREEN = \033[0;32m
    YELLOW = \033[1;33m
    NC = \033[0m
endif

TARGET_BIN = $(TARGET)$(EXE)
UNIT_BIN   = tests/unit_value$(EXE)
UNIT_TYPES = tests/unit_types$(EXE)
UNIT_GC    = tests/unit_gc$(EXE)

# Regra principal
all: $(TARGET_BIN)
	@echo "$(GREEN)✓ Compilação concluída com sucesso!$(NC)"
	@echo "$(YELLOW)Execute: ./$(TARGET_BIN) program.cinza$(NC)"

# Linkagem
$(TARGET_BIN): $(OBJECTS)
	@echo "$(YELLOW)Linkando...$(NC)"
	$(CXX) $(CXXFLAGS) -o $(TARGET_BIN) $(OBJECTS) $(LDFLAGS)

# Compilação dos arquivos objeto (dependências de headers via -MMD -MP)
%.o: %.cpp
	@echo "$(YELLOW)Compilando $<...$(NC)"
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEPS)

# Build de depuração com sanitizers
debug: CXXFLAGS = -std=c++20 -O0 -g -Wall -Wextra -fsanitize=address,undefined
debug: clean $(TARGET_BIN)

# CVM em construção: a mesma suíte executada na máquina virtual
test-cvm: $(TARGET_BIN)
	$(PYTHON) tests/run_tests.py $(TARGET_BIN) --cvm

# Teste diferencial: interpretador × CVM, saída comparada byte a byte
test-diff: $(TARGET_BIN)
	$(PYTHON) tests/run_tests.py $(TARGET_BIN) --diff

# Roda a suíte de testes em tests/
test: $(TARGET_BIN) $(UNIT_BIN) $(UNIT_TYPES) $(UNIT_GC)
	./$(UNIT_BIN)
	./$(UNIT_TYPES)
	./$(UNIT_GC)
	$(PYTHON) tests/run_tests.py $(TARGET_BIN)

# Testes de unidade em C++ (itens sem reprodução em .cinza, ex.: A11)
$(UNIT_BIN): tests/unit_value.cpp value.h ast.h lexer.h types.h gc_object.h
	$(CXX) $(CXXFLAGS) -o $(UNIT_BIN) tests/unit_value.cpp

$(UNIT_TYPES): tests/unit_types.cpp types.h
	$(CXX) $(CXXFLAGS) -o $(UNIT_TYPES) tests/unit_types.cpp

$(UNIT_GC): tests/unit_gc.cpp gc.h gc_object.h value.h
	$(CXX) $(CXXFLAGS) -o $(UNIT_GC) tests/unit_gc.cpp

# Testa mostrando tokens
test-tokens: $(TARGET_BIN)
	@echo "$(GREEN)Executando teste com tokens detalhados...$(NC)"
	./$(TARGET_BIN) --tokens program.cinza

# Testa mostrando AST
test-ast: $(TARGET_BIN)
	@echo "$(GREEN)Executando teste mostrando AST...$(NC)"
	./$(TARGET_BIN) --ast program.cinza

# Limpeza
clean:
	@echo "$(YELLOW)Limpando arquivos de compilação...$(NC)"
	-$(RM) $(call fixpath,$(OBJECTS) $(DEPS)) $(TARGET_BIN) $(call fixpath,$(UNIT_BIN) $(UNIT_TYPES) $(UNIT_GC))
	@echo "$(GREEN)✓ Limpeza concluída!$(NC)"

# Rebuild completo
rebuild: clean all

# Instala dependências (se necessário)
deps:
	@echo "$(YELLOW)Verificando compilador C++20...$(NC)"
	@$(CXX) --version || (echo "$(RED)✗ g++ não encontrado!$(NC)" && exit 1)
	@echo "$(GREEN)✓ Compilador OK!$(NC)"

# Ajuda
help:
	@echo "Makefile do Compilador Cinza"
	@echo ""
	@echo "Targets disponíveis:"
	@echo "  make              - Compila o projeto"
	@echo "  make test         - Compila e roda a suíte em tests/"
	@echo "  make debug        - Recompila com -O0 -g e sanitizers"
	@echo "  make test-tokens  - Mostra tokens detalhados"
	@echo "  make test-ast     - Mostra AST detalhada"
	@echo "  make clean        - Remove arquivos compilados"
	@echo "  make rebuild      - Limpa e recompila tudo"
	@echo "  make deps         - Verifica dependências"
	@echo "  make help         - Mostra esta ajuda"

.PHONY: all clean test test-tokens test-ast rebuild deps help debug
