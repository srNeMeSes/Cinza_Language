# Makefile para o Compilador da Linguagem Cinza
# C++20, otimização -O3, warnings habilitados

CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -pedantic
TARGET = cinza
SOURCES = main.cpp lexer.cpp parser.cpp ast.cpp semantic.cpp executor.cpp
HEADERS = lexer.h parser.h ast.h semantic.h value.h environment.h runtime_error.h executor.h
OBJECTS = $(SOURCES:.cpp=.o)

# Cores para output
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[1;33m
NC = \033[0m # No Color

# Regra principal
all: $(TARGET)
	@echo "$(GREEN)✓ Compilação concluída com sucesso!$(NC)"
	@echo "$(YELLOW)Execute: ./$(TARGET) exemplo.cinza$(NC)"

# Linkagem
$(TARGET): $(OBJECTS)
	@echo "$(YELLOW)Linkando...$(NC)"
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS)

# Compilação dos arquivos objeto
%.o: %.cpp $(HEADERS)
	@echo "$(YELLOW)Compilando $<...$(NC)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Testa com o arquivo de exemplo
test: $(TARGET)
	@echo "$(GREEN)Executando teste com exemplo.cinza...$(NC)"
	./$(TARGET) teste.cinza

# Testa mostrando tokens
test-tokens: $(TARGET)
	@echo "$(GREEN)Executando teste com tokens detalhados...$(NC)"
	./$(TARGET) teste.cinza --tokens

# Testa mostrando AST
test-ast: $(TARGET)
	@echo "$(GREEN)Executando teste mostrando AST...$(NC)"
	./$(TARGET) teste.cinza --ast

# Limpeza
clean:
	@echo "$(YELLOW)Limpando arquivos de compilação...$(NC)"
	rm -f $(OBJECTS) $(TARGET)
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
	@echo "  make test         - Compila e executa com teste.cinza"
	@echo "  make test-tokens  - Mostra tokens detalhados"
	@echo "  make test-ast     - Mostra AST detalhada"
	@echo "  make clean        - Remove arquivos compilados"
	@echo "  make rebuild      - Limpa e recompila tudo"
	@echo "  make deps         - Verifica dependências"
	@echo "  make help         - Mostra esta ajuda"

.PHONY: all clean test test-tokens test-ast rebuild deps help
