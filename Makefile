# Nome do compilador
CC = gcc

# Flags de compilação
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -lm

# Diretórios
BIN_DIR = bin

# Alvos principais
TARGETS = $(BIN_DIR)/server $(BIN_DIR)/client

# Regra padrão
all: $(TARGETS)

# Regra para criar o diretório bin
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Regra para compilar o servidor
$(BIN_DIR)/server: server.c common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) server.c -o $@

# Regra para compilar o cliente
$(BIN_DIR)/client: client.c common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) client.c -o $@ $(LDFLAGS)

# Limpeza dos binários
clean:
	rm -rf $(BIN_DIR)
