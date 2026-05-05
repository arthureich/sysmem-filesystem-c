#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512           // Tamanho de cada bloco em bytes.
#define RES_BLCK_COUNT 1         // Número de blocos reservados
#define BM_BLCK_COUNT 2          // Número de blocos para o bitmap
#define ET_BLCK_COUNT 10         // Número de blocos para a tabela de entradas

// Estruturas
typedef struct
{
    unsigned short bytes_per_block;               // offset 0  - 2 bytes. -> 512 bytes
    unsigned short reserved_blocks_count;         // offset 2  - 2 bytes. -> 1 block
    unsigned int number_of_bitmap_blocks;         // offset 4  - 4 bytes.
    unsigned int number_of_entry_table_blocks;    // offset 8  - 4 bytes.
    unsigned int number_of_data_section_blocks;   // offset 12 - 4 bytes.
    unsigned int number_of_total_system_blocks;   // offset 16 - 4 bytes.
    unsigned int number_of_root_directory_blocks; // offset 20 - 4 bytes.
    unsigned short first_root_directory_block;    // offset 24 - 2 bytes.
    unsigned char empty[6];
} __attribute__((__packed__)) BootRecord;

typedef struct
{
    unsigned char status;              // offset 0  - 1 byte.
    unsigned char file_name[12];       // offset 1  - 12 bytes.
    unsigned char file_extension[4];   // offset 13 - 4 bytes.
    unsigned char file_attribute;      // offset 17 - 1 byte.
    unsigned int first_block;          // offset 18 - 4 bytes.
    unsigned int size_in_bytes;        // offset 22 - 4 bytes.
    unsigned int total_used_blocks;    // offset 26 - 4 bytes.
    unsigned char empty[2];            // offset 30 - 2 bytes.
} __attribute__((__packed__)) Entries; // total size -> 32 bytes.

typedef struct
{
    unsigned char *blocks; // Sequência de bytes representando os blocos.
    unsigned int size;     // Tamanho do bitmap em bytes.
} Bitmap;

// Data Section.
typedef struct
{
    unsigned char *data; // Área de dados.
    unsigned int size;
} DataArea;

// Estrutura para os Diretórios
typedef struct Directory
{
    char directory_name[50];
    struct Directory *subdirectories;
    struct Directory *next;
} Directory;

// Protótipos de Funções
void createFileSystemImage(unsigned int partition_size);
void formatFileSystem(unsigned int partition_size);
void copyFileToFS(const char *source_file);
void copyFileFromFS(const char *file_name);
void listFiles();
void deleteFile(const char *file_name);
void addDirectory(const char *directory_name);
Directory *findDirectory(const char *directory_name);
void navigate();
void listDirectoryTree(Directory *root, int level);
void clearInputBuffer();

// Variáveis Globais
BootRecord boot_record;
Bitmap bitmap;
Entries *table_entries;
DataArea data_area;
Directory *root_directory = NULL;

int main()
{
    unsigned int partition_size;

    printf("Informe o tamanho em setores da particao: ");
    scanf("%u", &partition_size);

    createFileSystemImage(partition_size);
    navigate();

    // Libera a memória alocada dinamicamente
    free(bitmap.blocks);
    free(table_entries);
    free(data_area.data);

    return 0;
}

// Método que cria uma imagem do sistema de arquivos.
void createFileSystemImage(unsigned int partition_size)
{
    formatFileSystem(partition_size);

    printf("Sistema de arquivos criado com sucesso.\n");
}

// Método que formata o sistema de arquivos.
void formatFileSystem(unsigned int partition_size)
{
    // Preenche o boot record
    boot_record.bytes_per_block = BLOCK_SIZE;
    boot_record.reserved_blocks_count = RES_BLCK_COUNT;
    boot_record.number_of_bitmap_blocks = BM_BLCK_COUNT;
    boot_record.number_of_entry_table_blocks = ET_BLCK_COUNT;
    boot_record.number_of_data_section_blocks = partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT;
    boot_record.number_of_total_system_blocks = partition_size;
    boot_record.number_of_root_directory_blocks = 1;
    boot_record.first_root_directory_block = RES_BLCK_COUNT + BM_BLCK_COUNT + ET_BLCK_COUNT;

    // Aloca espaço para o bitmap
    bitmap.blocks = (unsigned char *)malloc(partition_size * sizeof(unsigned char));
    bitmap.size = partition_size;

    // Preenche o bitmap
    memset(bitmap.blocks, 0, partition_size * sizeof(unsigned char));

    // Aloca espaço para a tabela de entradas
    table_entries = (Entries *)malloc(ET_BLCK_COUNT * BLOCK_SIZE);
    if (table_entries == NULL)
    {
        printf("Erro ao alocar memoria para a tabela de entradas.\n");
        exit(1);
    }

    // Preenche as entradas da tabela de diretórios
    memset(table_entries, 0, ET_BLCK_COUNT * BLOCK_SIZE);

    // Aloca espaço para a área de dados
    data_area.data = (unsigned char *)malloc((partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT) * BLOCK_SIZE);
    if (data_area.data == NULL)
    {
        printf("Erro ao alocar memoria para a area de dados.\n");
        exit(1);
    }
    data_area.size = (partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT) * BLOCK_SIZE;

    // Limpa a área de dados
    memset(data_area.data, 0, (partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT) * BLOCK_SIZE);
}

// Método que copia um arquivo do disco rígido para o sistema de arquivos.
void copyFileToFS(const char *source_file)
{
    // Abre o arquivo fonte
    FILE *source = fopen(source_file, "rb");
    if (source == NULL)
    {
        printf("Erro ao abrir o arquivo fonte.\n");
        return;
    }

    // Encontra um bloco de dados disponível no bitmap
    int available_block = -1;
    for (int i = 0; i < boot_record.number_of_total_system_blocks; i++)
    {
        if (bitmap.blocks[i] == 0)
        {
            available_block = i;
            bitmap.blocks[i] = 1; // Marca o bloco como ocupado
            break;
        }
    }

    if (available_block == -1)
    {
        printf("Nao ha espaço disponivel no sistema de arquivos.\n");
        fclose(source);
        return;
    }

    // Copia o conteúdo do arquivo para a área de dados
    fseek(source, 0, SEEK_END);
    long file_size = ftell(source);
    rewind(source);

    if (file_size > data_area.size)
    {
        printf("Arquivo muito grande para o sistema de arquivos.\n");
        fclose(source);
        return;
    }

    fread(&data_area.data[available_block * BLOCK_SIZE], sizeof(unsigned char), file_size, source);

    fclose(source);

    // Adiciona uma entrada para o arquivo na tabela de diretórios
    int entry_index = 0;
    while (table_entries[entry_index].status != 0)
    {
        entry_index++;
        if (entry_index >= ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries))
        {
            printf("Tabela de entradas cheia.\n");
            return;
        }
    }

    strcpy(table_entries[entry_index].file_name, source_file);
    table_entries[entry_index].status = 1;
    table_entries[entry_index].file_attribute = 0;
    table_entries[entry_index].first_block = available_block;
    table_entries[entry_index].size_in_bytes = file_size;

    printf("Arquivo copiado para o sistema de arquivos com sucesso.\n");
}

// Método que copia um arquivo do sistema de arquivos para o disco rígido.
void copyFileFromFS(const char *file_name)
{
    // Procura o arquivo na tabela de entradas
    int entry_index = -1;
    for (int i = 0; i < ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries); i++)
    {
        if (table_entries[i].status != 0 && strcmp(table_entries[i].file_name, file_name) == 0)
        {
            entry_index = i;
            break;
        }
    }

    if (entry_index == -1)
    {
        printf("Arquivo nao encontrado no sistema de arquivos.\n");
        return;
    }

    // Abre o arquivo destino no disco rígido
    char dest_file[50];
    strcpy(dest_file, "copied_");
    strcat(dest_file, file_name);

    FILE *dest = fopen(dest_file, "wb");
    if (dest == NULL)
    {
        printf("Erro ao abrir o arquivo de destino.\n");
        return;
    }

    // Escreve o conteúdo do arquivo na área de dados
    fwrite(&data_area.data[table_entries[entry_index].first_block * BLOCK_SIZE], sizeof(unsigned char), table_entries[entry_index].size_in_bytes, dest);

    fclose(dest);

    printf("Arquivo copiado para o disco rigido com sucesso.\n");
}

// Método que lista os arquivos armazenados no sistema de arquivos.
void listFiles()
{
    printf("Arquivos armazenados no sistema de arquivos:\n");

    for (int i = 0; i < ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries); i++)
    {
        if (table_entries[i].status != 0)
        {
            printf("%s\n", table_entries[i].file_name);
        }
    }
}

// Método que remove um arquivo do sistema de arquivos.
void deleteFile(const char *file_name)
{
    // Procura o arquivo na tabela de entradas
    int entry_index = -1;
    for (int i = 0; i < ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries); i++)
    {
        if (table_entries[i].status != 0 && strcmp(table_entries[i].file_name, file_name) == 0)
        {
            entry_index = i;
            break;
        }
    }

    if (entry_index == -1)
    {
        printf("Arquivo nao encontrado no sistema de arquivos.\n");
        return;
    }

    // Libera os blocos de dados ocupados pelo arquivo no bitmap
    bitmap.blocks[table_entries[entry_index].first_block] = 0;

    // Limpa a entrada da tabela de entradas
    memset(&table_entries[entry_index], 0, sizeof(Entries));

    printf("Arquivo removido do sistema de arquivos.\n");
}

// Método que navega pelo sistema de arquivos.
void navigate()
{
    int option;
    do
    {
        printf("\nSistema de Arquivos - Opcoes:\n");
        printf("[1] - Copiar arquivo para o sistema de arquivos\n");
        printf("[2] - Copiar arquivo do sistema de arquivos para o disco rigido\n");
        printf("[3] - Listar arquivos no sistema de arquivos\n");
        printf("[4] - Remover arquivo do sistema de arquivos\n");
        printf("[5] - Adicionar diretorio\n");
        printf("[6] - Listar diretorios\n");
        printf("[7] - Sair\n");
        printf("Escolha uma opcao: ");
        scanf("%d", &option);
        clearInputBuffer();

        switch (option)
        {
        case 1:
        {
            char source_file[50];
            printf("Digite o nome do arquivo a ser copiado para o sistema de arquivos: ");
            fgets(source_file, 50, stdin);
            source_file[strcspn(source_file, "\n")] = 0; // Remove a quebra de linha do final

            copyFileToFS(source_file);
            break;
        }
        case 2:
        {
            char file_name[50];
            printf("Digite o nome do arquivo a ser copiado do sistema de arquivos para o disco rigido: ");
            fgets(file_name, 50, stdin);
            file_name[strcspn(file_name, "\n")] = 0; // Remove a quebra de linha do final

            copyFileFromFS(file_name);
            break;
        }
        case 3:
            listFiles();
            break;
        case 4:
        {
            char file_name[50];
            printf("Digite o nome do arquivo a ser removido do sistema de arquivos: ");
            fgets(file_name, 50, stdin);
            file_name[strcspn(file_name, "\n")] = 0; // Remove a quebra de linha do final

            deleteFile(file_name);
            break;
        }
        case 5:
        {
            char directory_name[50];
            printf("Digite o nome do diretorio a ser adicionado: ");
            fgets(directory_name, 50, stdin);
            directory_name[strcspn(directory_name, "\n")] = 0; // Remove a quebra de linha do final

            addDirectory(directory_name);
            break;
        }
        case 6:
            printf("\nArvore de diretorios:\n");
            listDirectoryTree(root_directory, 0);
            break;
        case 7:
            printf("Encerrando...\n");
            break;
        default:
            printf("Opcao invalida.\n");
            break;
        }
    } while (option != 7);
}


// Método que adiciona um diretório
void addDirectory(const char *directory_name)
{
    Directory *new_directory = (Directory *)malloc(sizeof(Directory));
    if (new_directory != NULL)
    {
        strcpy(new_directory->directory_name, directory_name);
        new_directory->subdirectories = NULL;
        new_directory->next = NULL;

        if (root_directory == NULL)
        {
            root_directory = new_directory;
        }
        else
        {
            Directory *temp = root_directory;
            while (temp->next != NULL)
            {
                temp = temp->next;
            }
            temp->next = new_directory;
        }
    }
}

// Método que encontra um diretório
Directory *findDirectory(const char *directory_name)
{
    Directory *temp = root_directory;
    while (temp != NULL)
    {
        if (strcmp(temp->directory_name, directory_name) == 0)
        {
            return temp;
        }
        temp = temp->next;
    }
    return NULL;
}

// Método que lista os diretórios e arquivos em forma de árvore
void listDirectoryTree(Directory *root, int level)
{
    if (root == NULL)
        return;

    for (int i = 0; i < level; i++)
        printf("  ");

    printf("- %s\n", root->directory_name);

    listDirectoryTree(root->subdirectories, level + 1);
    listDirectoryTree(root->next, level);
}

// Limpa o buffer de entrada
void clearInputBuffer()
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}
