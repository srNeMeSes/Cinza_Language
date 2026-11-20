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
    # Q5: estático, para rodar fora do ambiente de compilação (sem as DLLs do GCC)
    LDFLAGS = -static -Wl,--stack,67108864
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
    rmrf = if exist $(subst /,\,$1) rmdir /S /Q $(subst /,\,$1)
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
    rmrf = rm -rf $1
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
debug: LDFLAGS := $(filter-out -static,$(LDFLAGS))
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
# cinzac: o compilador nativo (llvm/DESENHO.md; seções P e Q5 do REVISAO_V2.md), pelo LLVM 20.
# Fica em llvm/bin/, com a pasta runtime/ ao lado: a libcinzart.a (o runtime que os programas
# gerados ligam) e, no Windows, as bibliotecas de partida e a do C++, para o cinzac ligar sozinho.
# - Windows (Q5): o cinzac é um executável só, sem DLLs. O LLVM e o lld vão estáticos (o lld liga
#   os programas dentro do cinzac, llvm/ligador.cpp), e por isso o cinzac e o runtime são
#   compilados pelo clang do MSYS2 CLANG64, com a libc++ das bibliotecas do LLVM (LLVM_PREFIX
#   troca a pasta), em objetos à parte dos da CVM (build/cinzac/). Do CLANG64 vêm também, para
#   runtime/, os arquivos de partida, a libc++ e as bibliotecas do mingw-w64.
# - Linux (Q4): o LLVM 20 do sistema, pelo llvm-config-20 ou llvm-config (LLVM_CONFIG troca), com
#   o compilador do projeto; os programas são ligados pelo g++. O llvm-config só roda quando o
#   cinzac é pedido: o `make` da CVM não precisa do LLVM.
ifneq ($(OS)$(MSYSTEM),)
    LLVM_PREFIX  ?= C:/msys64/clang64
    LLVM_CONFIG  ?= $(LLVM_PREFIX)/bin/llvm-config
    LLVM_INCLUDE  = $(LLVM_PREFIX)/include
    CINZAC_CXX   ?= $(LLVM_PREFIX)/bin/clang++
    CINZAC_AR    ?= $(LLVM_PREFIX)/bin/llvm-ar
    CINZAC_FLAGS  = -DCINZA_LLD
    # estático, sem os símbolos e sem as seções não usadas (de 134 MB para ~47 MB)
    CINZAC_STATIC = -static -Wl,--gc-sections -s
    # o lld embutido, o libxml2 estático no lugar da DLL e só o alvo x86 (os outros alvos com
    # inicializações vazias: o lld pede todos, por causa do LTO)
    CINZAC_LLD    = llvm/ligador.cpp llvm/xml_estatico.cpp llvm/alvos_vazios.cpp
    # o lld (MinGW e o COFF que ele usa) antes do LLVM, de que depende; do LLVM, só os componentes
    # que o cinzac e o lld usam; as bibliotecas do sistema do LLVM estático (o zstd estático, não o
    # da DLL), as do libxml2 e o bcrypt da libc++
    LLVM_COMPS    = x86 passes lto libdriver windowsdriver windowsmanifest debuginfopdb option
    LLVM_LIBS     = -llldMinGW -llldCOFF -llldCommon $(shell $(LLVM_CONFIG) --link-static --libs $(LLVM_COMPS)) \
                    $(subst -lzstd.dll,-lzstd,$(shell $(LLVM_CONFIG) --link-static --system-libs)) \
                    -liconv -llzma -lbcrypt
    RUNTIME_LIBS  = crt2.o crtbegin.o crtend.o libc++.a libunwind.a libmingw32.a libmingwex.a libmoldname.a \
                    libmsvcrt.a libadvapi32.a libshell32.a libuser32.a libkernel32.a libwinpthread.a libbcrypt.a
    RUNTIME_RT    = $(LLVM_PREFIX)/lib/clang/20/lib/windows/libclang_rt.builtins-x86_64.a
else
    LLVM_CONFIG  ?= $(firstword $(shell command -v llvm-config-20 llvm-config 2>/dev/null) llvm-config)
    LLVM_INCLUDE  = $(shell $(LLVM_CONFIG) --includedir)
    CINZAC_CXX   ?= $(CXX)
    CINZAC_AR    ?= ar
    CINZAC_FLAGS  =
    CINZAC_STATIC =
    CINZAC_LLD    =
    LLVM_LIBS     = $(shell $(LLVM_CONFIG) --ldflags) $(shell $(LLVM_CONFIG) --libs) \
                    -Wl,-rpath,$(shell $(LLVM_CONFIG) --libdir)
    RUNTIME_LIBS  =
    RUNTIME_RT    =
    # Q5: a distribuição do Linux (make ESTATICO=1, no Alpine, com a musl): a CVM e o cinzac
    # estáticos, que rodam em qualquer Linux x86-64; o lld ELF dentro do cinzac, e em runtime/ a
    # musl e as bibliotecas do C++ que o compilador usa (achadas por -print-file-name), para os
    # programas gerados também saírem estáticos
    ifeq ($(ESTATICO),1)
        LDFLAGS       = -static
        CINZAC_FLAGS  = -DCINZA_LLD
        CINZAC_STATIC = -static -Wl,--gc-sections -s
        CINZAC_LLD    = llvm/ligador.cpp llvm/alvos_vazios.cpp
        LLVM_COMPS    = x86 passes lto option
        # LLD_LIBDIR: onde estão as bibliotecas estáticas do lld (o Alpine só traz as .so; o CI as
        # compila da fonte, com o mesmo LLVM)
        LLVM_LIBS     = $(addprefix -L,$(LLD_LIBDIR)) $(shell $(LLVM_CONFIG) --ldflags) -llldELF -llldCommon \
                        $(shell $(LLVM_CONFIG) --link-static --libs $(LLVM_COMPS)) \
                        $(shell $(LLVM_CONFIG) --link-static --system-libs)
        RUNTIME_LIBS  = crt1.o crti.o crtn.o crtbeginT.o crtend.o libc.a libstdc++.a libgcc.a libgcc_eh.a
    endif
endif
LLVM_BIN    = llvm/bin
RUNTIME_DIR = $(LLVM_BIN)/runtime
CINZAC_BIN  = $(LLVM_BIN)/cinzac$(EXE)
LIBCINZART  = $(RUNTIME_DIR)/libcinzart.a
CZ_DIR      = build/cinzac
CINZAC_SRC  = lexer.cpp parser.cpp ast.cpp semantic.cpp module_loader.cpp biblioteca.cpp operacoes.cpp \
              natives.cpp stdlib.cpp regex.cpp plataforma.cpp llvm/cir.cpp llvm/gera_cir.cpp llvm/emissor.cpp \
              llvm/cinzac.cpp $(CINZAC_LLD)
RUNTIME_SRC = operacoes.cpp natives.cpp stdlib.cpp regex.cpp plataforma.cpp llvm/cinzart.cpp
CINZAC_OBJ  = $(addprefix $(CZ_DIR)/,$(CINZAC_SRC:.cpp=.o))
RUNTIME_OBJ = $(addprefix $(CZ_DIR)/,$(RUNTIME_SRC:.cpp=.o))
RUNTIME_ARQ = $(addprefix $(RUNTIME_DIR)/,$(RUNTIME_LIBS) $(notdir $(RUNTIME_RT)))
$(CZ_DIR)/llvm/xml_estatico.o: CINZAC_FLAGS += -I$(LLVM_PREFIX)/include/libxml2
-include $(CINZAC_OBJ:.o=.d) $(CZ_DIR)/llvm/cinzart.d

cinzac: $(CINZAC_BIN) $(LIBCINZART) $(RUNTIME_ARQ)
	@echo "$(GREEN)✓ cinzac pronto em $(CINZAC_BIN)$(NC)"

$(CZ_DIR)/%.o: %.cpp
	@$(call mkdirp,$(patsubst %/,%,$(dir $@)))
	$(CINZAC_CXX) $(CXXFLAGS) $(CINZAC_FLAGS) -idirafter $(LLVM_INCLUDE) $(DEPFLAGS) -c $< -o $@

$(CINZAC_BIN): $(CINZAC_OBJ)
	@$(call mkdirp,$(LLVM_BIN))
	$(CINZAC_CXX) $(CXXFLAGS) $(CINZAC_STATIC) -o $@ $(CINZAC_OBJ) $(LLVM_LIBS) $(LDFLAGS)

$(LIBCINZART): $(RUNTIME_OBJ)
	@$(call mkdirp,$(RUNTIME_DIR))
	$(CINZAC_AR) rcs $@ $(RUNTIME_OBJ)

ifneq ($(OS)$(MSYSTEM),)
$(RUNTIME_DIR)/%: $(LLVM_PREFIX)/lib/%
	@$(call mkdirp,$(RUNTIME_DIR))
	$(COPY) "$(call fixpath,$<)" "$(call fixpath,$@)"

$(RUNTIME_DIR)/$(notdir $(RUNTIME_RT)): $(RUNTIME_RT)
	@$(call mkdirp,$(RUNTIME_DIR))
	$(COPY) "$(call fixpath,$<)" "$(call fixpath,$@)"
else
$(RUNTIME_DIR)/%:
	@$(call mkdirp,$(RUNTIME_DIR))
	cp "$$($(CINZAC_CXX) -print-file-name=$*)" "$@"
endif

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
cinzac-debug: CINZAC_STATIC =
# dinâmico (o ASan do mingw é uma DLL): o libxml2 vem da DLL, sem a ponte estática
cinzac-debug: CINZAC_LLD = llvm/ligador.cpp llvm/alvos_vazios.cpp
cinzac-debug: LDFLAGS := $(filter-out -static,$(LDFLAGS))
cinzac-debug: clean cinzac

# detect_container_overflow=0: o LLVM estático não é instrumentado, e as anotações dos contêineres da
# libc++ entre ele e o código instrumentado do cinzac dão falso positivo (o resto do ASan continua)
test-llvm-debug: export ASAN_OPTIONS = detect_container_overflow=0
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
	-$(call rmrf,$(CZ_DIR))
	-$(call rmrf,$(RUNTIME_DIR))
	-$(RM) $(call fixpath,$(CINZAC_BIN))
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
