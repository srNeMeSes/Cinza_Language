# Makefile para o Compilador da Linguagem Cinza
# C++20, otimização -O3, warnings habilitados
# No PowerShell (MinGW) o comando é `mingw32-make`; no MSYS2 e no Linux é `make`.
# `make debug` (ASan/UBSan) no Windows só funciona pelo MSYS2 CLANG64.
# O compilador precisa de C++20 e do #embed (biblioteca.cpp): GCC 15 ou Clang 19 em diante
# (no Linux, `make CXX=clang++-20` troca o compilador).

CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -pedantic
DEPFLAGS = -MMD -MP
TARGET = cinza
SOURCES = main.cpp lexer.cpp parser.cpp ast.cpp semantic.cpp operacoes.cpp natives.cpp stdlib.cpp regex.cpp plataforma.cpp module_loader.cpp biblioteca.cpp \
          cvm/compiler.cpp cvm/vm.cpp cvm/disasm.cpp
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

# No MSYS2 CLANG64 não existe g++, só clang++ (é o ambiente com ASan no Windows)
ifeq ($(MSYSTEM),CLANG64)
    CXX = clang++
endif

# O binário ganha .exe em qualquer Windows (PowerShell, MSYS2).
# A6: pilha nativa de 64 MB (a padrão do Windows tem 1 MB), com folga para a
# recursão do compilador e do semântico em programas muito aninhados.
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
    COPY = copy /Y
    mkdirp = if not exist $(subst /,\,$1) mkdir $(subst /,\,$1)
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
    COPY = cp
    mkdirp = mkdir -p $1
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
debug: CXXFLAGS = -std=c++20 -O0 -g -Wall -Wextra -fsanitize=address,undefined -DCINZA_CONFERE_REFS
debug: clean $(TARGET_BIN)

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

# ---------------------------------------------------------------------------
# cinzac: o compilador nativo (llvm/DESENHO.md; seção P do REVISAO_V2.md), pela API em C
# do LLVM 20. Fica em llvm/bin/, com a libcinzart.a (o runtime que os programas gerados
# ligam).
# - Windows: o LLVM 20.1.8 do MSYS2 CLANG64 (mingw-w64-clang-x86_64-llvm; LLVM_PREFIX troca
#   a pasta) com o GCC do projeto, e as DLLs do LLVM ao lado do cinzac: o Windows as procura
#   primeiro na pasta do executável, antes do PATH (onde o GCC tem outras versões de zlib1,
#   libzstd...).
# - Linux (Q4): o LLVM 20 do sistema, pelo llvm-config-20 ou llvm-config (LLVM_CONFIG troca);
#   a biblioteca compartilhada do LLVM fica onde está (rpath). O llvm-config só roda quando o
#   cinzac é pedido: o `make` da CVM não precisa do LLVM.
ifneq ($(OS)$(MSYSTEM),)
    LLVM_PREFIX  ?= C:/msys64/clang64
    LLVM_INCLUDE  = $(LLVM_PREFIX)/include
    LLVM_LIBS     = $(LLVM_PREFIX)/lib/libLLVM-20.dll.a -static-libstdc++ -static-libgcc
    LLVM_DLLS     = libLLVM-20.dll libc++.dll libffi-8.dll libiconv-2.dll liblzma-5.dll libxml2-16.dll \
                    libzstd.dll zlib1.dll
else
    LLVM_CONFIG  ?= $(firstword $(shell command -v llvm-config-20 llvm-config 2>/dev/null) llvm-config)
    LLVM_INCLUDE  = $(shell $(LLVM_CONFIG) --includedir)
    LLVM_LIBS     = $(shell $(LLVM_CONFIG) --ldflags) $(shell $(LLVM_CONFIG) --libs) \
                    -Wl,-rpath,$(shell $(LLVM_CONFIG) --libdir)
    LLVM_DLLS     =
endif
LLVM_BIN    = llvm/bin
CINZAC_BIN  = $(LLVM_BIN)/cinzac$(EXE)
LIBCINZART  = $(LLVM_BIN)/libcinzart.a
RUNTIME_OBJ = operacoes.o natives.o stdlib.o regex.o plataforma.o llvm/cinzart.o
CINZAC_OBJ  = lexer.o parser.o ast.o semantic.o module_loader.o biblioteca.o operacoes.o natives.o stdlib.o regex.o \
              plataforma.o llvm/cir.o llvm/gera_cir.o llvm/emissor.o llvm/cinzac.o
CINZAC_DEPS = $(patsubst %.o,%.d,llvm/cinzart.o llvm/cir.o llvm/gera_cir.o llvm/emissor.o llvm/cinzac.o)
-include $(CINZAC_DEPS)

# só o emissor e o cinzac incluem a llvm-c; -idirafter: os cabeçalhos do CLANG64 não
# passam na frente dos do GCC
llvm/emissor.o llvm/cinzac.o: CXXFLAGS += -idirafter $(LLVM_INCLUDE)

cinzac: $(CINZAC_BIN) $(LIBCINZART) $(addprefix $(LLVM_BIN)/,$(LLVM_DLLS))
	@echo "$(GREEN)✓ cinzac pronto em $(CINZAC_BIN)$(NC)"

$(LLVM_BIN):
	$(call mkdirp,$(LLVM_BIN))

$(CINZAC_BIN): $(CINZAC_OBJ) | $(LLVM_BIN)
	$(CXX) $(CXXFLAGS) -o $@ $(CINZAC_OBJ) $(LLVM_LIBS) $(LDFLAGS)

$(LIBCINZART): $(RUNTIME_OBJ) | $(LLVM_BIN)
	ar rcs $@ $(RUNTIME_OBJ)

$(LLVM_BIN)/%.dll: $(LLVM_PREFIX)/bin/%.dll | $(LLVM_BIN)
	$(COPY) "$(call fixpath,$<)" "$(call fixpath,$@)"

# A suíte pelo cinzac (compila cada teste e roda o executável); os recursos que ele
# ainda não compila são contados à parte
test-llvm: cinzac
	./$(CINZAC_BIN) --confere-layout
	$(PYTHON) tests/run_tests.py --cinzac

# cinzac e libcinzart com ASan/UBSan (REVISAO Q2): só no MSYS2 CLANG64, como o make debug.
# A suíte pelo cinzac de depuração, com o código gerado também instrumentado (--asan) e os
# programas ligados com o runtime do ASan pelo clang++ (o UBSan também interrompe o programa, senão
# o aviso dele passaria despercebido num teste que só confere a saída):
#   make cinzac-debug && make test-llvm-debug PYTHON=/c/Python313/python
cinzac-debug: CXXFLAGS = -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined -DCINZA_CONFERE_REFS
cinzac-debug: clean cinzac

test-llvm-debug:
	./$(CINZAC_BIN) --confere-layout
	CINZA_GXX=clang++ CINZA_LIGACAO="-fsanitize=address,undefined" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(PYTHON) tests/run_tests.py --cinzac --asan

# A gramática da extensão do VS Code (editor/vscode): os trechos que ela marca como
# inválidos nos .cinza do repositório têm de ser os esperados (usa o VS Code instalado)
test-vscode:
	$(PYTHON) editor/vscode/teste/conferir.py

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
	-$(RM) $(call fixpath,llvm/cinzart.o llvm/cir.o llvm/gera_cir.o llvm/emissor.o llvm/cinzac.o $(CINZAC_DEPS) $(CINZAC_BIN) $(LIBCINZART))
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
	@echo "  make cinzac       - Compila o cinzac (backend LLVM) em llvm/bin/"
	@echo "  make test-llvm    - Roda a suíte pelo cinzac"
	@echo "  make test-vscode  - Confere a gramática da extensão do VS Code (editor/vscode)"
	@echo "  make cinzac-debug - cinzac e runtime com ASan/UBSan (MSYS2 CLANG64); depois make test-llvm-debug"
	@echo "  make test-tokens  - Mostra tokens detalhados"
	@echo "  make test-ast     - Mostra AST detalhada"
	@echo "  make clean        - Remove arquivos compilados"
	@echo "  make rebuild      - Limpa e recompila tudo"
	@echo "  make deps         - Verifica dependências"
	@echo "  make help         - Mostra esta ajuda"

.PHONY: all clean test test-tokens test-ast rebuild deps help debug cinzac test-llvm test-vscode         cinzac-debug test-llvm-debug
