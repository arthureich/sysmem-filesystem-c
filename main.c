#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512   // Tamanho de cada bloco em bytes.
#define RES_BLCK_COUNT 1 // Numero de blocos reservados.
#define BM_BLCK_COUNT 2  // Numero de blocos para o bitmap.
#define ET_BLCK_COUNT 10 // Numero de blocos para a tabela de entradas.

// ESTRUTURAS:

// BootRecord.
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

// Entradas.
typedef struct
{
    unsigned char status;              // offset 0  - 1 byte.
    unsigned char entry_name[12];      // offset 1  - 12 bytes.
    unsigned char entry_extension[4];  // offset 13 - 4 bytes.
    unsigned char file_attribute;      // offset 17 - 1 byte.
    unsigned int first_block;          // offset 18 - 4 bytes.
    unsigned int size_in_bytes;        // offset 22 - 4 bytes.
    unsigned int total_used_blocks;    // offset 26 - 4 bytes.
    unsigned char empty[2];            // offset 30 - 2 bytes.
} __attribute__((__packed__)) Entries; // total size -> 32 bytes.

// Mapa de Bits.
typedef struct
{
    unsigned char *blocks; // Sequencia de bytes representando os blocos.
    unsigned int size;     // Tamanho do bitmap em bytes.
} Bitmap;

// Data Section.
typedef struct
{
    unsigned char *data; // Area de dados.
    unsigned int size;
} DataArea;

// Estrutura para os Diretórios
typedef struct Directory
{
    char directory_name[50];
    Entries *files;
    struct Directory **subdirectories; // Alterado para vetor de ponteiros
    int index;
    int files_count; // Contador de arquivos
    int subdirectories_count; // Contador de subdiretórios
    struct Directory *next;
} Directory;

// Variáveis Globais.
BootRecord boot_record;
Bitmap bitmap;
Entries *table_entries;
DataArea data_area;
Directory *root_directory;

// Protótipos de Funções.
void loadFileSystemImage(const char *isoName);
void createFileSystemImage(unsigned int partition_size);
void formatFileSystem(unsigned int partition_size);
void navigate();
void clearInputBuffer();
void copyFileToFS(const char *source_file);
void copyFileFromFS(const char *entry_name);
void listFiles();
void deleteFile(const char *entry_name);
void addDirectory(const char *directory_name, int parent_index);
Directory* findDirectory(Directory *root, int index);
void listDirectoryTree(Directory *root, int level);
void saveFileSystemImage();

int main()
{
    char isoName[30]; // Armazena o nome da imagem do sistema.

    printf("Informe o nome da imagem a ser carregada (Nome invalido criara nova): ");
    fgets(isoName, 30, stdin);
    isoName[strcspn(isoName, "\n")] = '\0'; // Remove a quebra de linha.

    // Ou cria nova imagem ou carrega imagem existente.
    loadFileSystemImage(isoName);
    // Manipulação do sistema de arquivos.
    navigate();
    // Salva alterações realizadas.
    saveFileSystemImage(isoName);

    // Libera a memória das estruturas alocadas dinamicamente.
    free(bitmap.blocks);
    free(table_entries);
    free(data_area.data);

    // Libera a memória dos diretórios e arquivos
    Directory *current_dir = root_directory;
    while (current_dir != NULL)
    {
        free(current_dir->files);
        for (int i = 0; i < current_dir->subdirectories_count; i++)
            free(current_dir->subdirectories[i]);
        free(current_dir->subdirectories);
        Directory *temp = current_dir->next;
        free(current_dir);
        current_dir = temp;
    }

    return 0;
}

// Carrega ou cria a imagem do sistema de arquivos.
void loadFileSystemImage(const char *isoName)
{
    FILE *file = fopen(isoName, "rb");
    if (file == NULL)
    {
        printf("Nenhuma imagem do sistema de arquivos encontrada. Criando uma nova imagem...\n");
        createFileSystemImage(1000); // Tamanho padrão de 1000 blocos.
        return;
    }

    // Leitura do boot record.
    fread(&boot_record, sizeof(BootRecord), 1, file);

    // Inicialização do bitmap.
    bitmap.blocks = (unsigned char *)malloc(boot_record.number_of_total_system_blocks * sizeof(unsigned char));
    bitmap.size = boot_record.number_of_total_system_blocks;

    // Leitura do bitmap.
    fread(bitmap.blocks, sizeof(unsigned char), boot_record.number_of_total_system_blocks, file);

    // Inicialização para a tabela de entradas.
    table_entries = (Entries *)malloc(ET_BLCK_COUNT * BLOCK_SIZE);
    if (table_entries == NULL)
    {
        printf("Erro ao alocar memoria para a tabela de entradas.\n");
        exit(1);
    }
    Directory *root = (Directory *)malloc(sizeof(Directory));
    root_directory = root;
    root_directory->index = 0;
    root_directory->subdirectories_count=0;
    root_directory->files_count=0;
    // Preenche as entradas da tabela de diretorios.
    memset(table_entries, 0, ET_BLCK_COUNT * BLOCK_SIZE);

    // Aloca espaço para a area de dados.
    data_area.data = (unsigned char *)malloc((boot_record.number_of_data_section_blocks) * BLOCK_SIZE);
    if (data_area.data == NULL)
    {
        printf("Erro ao alocar memoria para a area de dados.\n");
        exit(1);
    }
    data_area.size = (boot_record.number_of_data_section_blocks) * BLOCK_SIZE;

    // Leitura da area de dados.
    fread(data_area.data, sizeof(unsigned char), boot_record.number_of_data_section_blocks * BLOCK_SIZE, file);

    fclose(file);

    printf("Imagem do sistema de arquivos carregada com sucesso.\n");
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
    // Preenche o boot record.
    boot_record.bytes_per_block = BLOCK_SIZE;
    boot_record.reserved_blocks_count = RES_BLCK_COUNT;
    boot_record.number_of_bitmap_blocks = BM_BLCK_COUNT;
    boot_record.number_of_entry_table_blocks = ET_BLCK_COUNT;
    boot_record.number_of_data_section_blocks = partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT;
    boot_record.number_of_total_system_blocks = partition_size;
    boot_record.number_of_root_directory_blocks = 1;
    boot_record.first_root_directory_block = RES_BLCK_COUNT + BM_BLCK_COUNT + ET_BLCK_COUNT;

    // Aloca espaço para o bitmap.
    bitmap.blocks = (unsigned char *)malloc(partition_size * sizeof(unsigned char));
    bitmap.size = partition_size;

    // Preenche o bitmap.
    memset(bitmap.blocks, 0, partition_size * sizeof(unsigned char));

    // Aloca espaço para a tabela de entradas.
    table_entries = (Entries *)malloc(ET_BLCK_COUNT * BLOCK_SIZE);
    if (table_entries == NULL)
    {
        printf("Erro ao alocar memoria para a tabela de entradas.\n");
        exit(1);
    }
    Directory *root = (Directory *)malloc(sizeof(Directory));
    root_directory = root;
    root_directory->index = 0;
    root_directory->subdirectories_count=0;
    root_directory->files_count=0;
    // Preenche as entradas da tabela de diretorios.
    memset(table_entries, 0, ET_BLCK_COUNT * BLOCK_SIZE);

    // Aloca espaço para a area de dados.
    data_area.data = (unsigned char *)malloc((partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT) * BLOCK_SIZE);
    if (data_area.data == NULL)
    {
        printf("Erro ao alocar memoria para a area de dados.\n");
        exit(1);
    }
    data_area.size = (partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT) * BLOCK_SIZE;

    // Limpa a area de dados.
    memset(data_area.data, 0, (partition_size - RES_BLCK_COUNT - BM_BLCK_COUNT - ET_BLCK_COUNT) * BLOCK_SIZE);
}

// Método que navega pelo sistema de arquivos.
void navigate()
{
    unsigned int partition_size;
    int option;

    do
    {

        printf("\nSistema de Arquivos - Opcoes:\n");
        printf("[1] - Copiar arquivo para o sistema de arquivos\n");
        printf("[2] - Copiar arquivo do sistema de arquivos para o disco rigido\n");
        printf("[3] - Listar arquivos do sistema de arquivos\n");
        printf("[4] - Remover arquivo do sistema de arquivos\n");
        printf("[5] - Adicionar diretorio\n");
        printf("[6] - Listar diretorios\n");
        printf("[7] - Formatar Particao\n");
        printf("[8] - Sair\n");
        printf("Escolha uma opcao: ");
        scanf("%d", &option);
        clearInputBuffer();

        switch (option)
        {
        case 1:
        {
            char file[50];

            printf("Digite o nome do arquivo a ser copiado para o sistema de arquivos: ");
            fgets(file, 50, stdin);
            file[strcspn(file, "\n")] = 0; // Remove a quebra de linha do final.

            // Copia arquivo do disco para o sistema de arquivos.
            copyFileToFS(file);
            break;
        }
        case 2:
        {
            char entry_name[50];

            printf("Digite o nome do arquivo a ser copiado do sistema de arquivos para o disco rigido: ");
            fgets(entry_name, 50, stdin);
            entry_name[strcspn(entry_name, "\n")] = 0; // Remove a quebra de linha do final.

            // Copia o arquivo do sistema de arquivos para o disco rígido.
            copyFileFromFS(entry_name);
            break;
        }
        case 3:
            // Exibe os arquivos.
            listFiles();
            break;
        case 4:
        {
            char entry_name[50];

            printf("Digite o nome do arquivo a ser removido: ");
            fgets(entry_name, 50, stdin);
            entry_name[strcspn(entry_name, "\n")] = 0; // Remove a quebra de linha do final.

            // Remove o arquivo.
            deleteFile(entry_name);
            break;
        }
        case 5:
        {
            char directory_name[50];
            int parent_index;

            printf("Digite o nome do diretorio a ser adicionado: ");
            fgets(directory_name, 50, stdin);
            directory_name[strcspn(directory_name, "\n")] = 0; // Remove a quebra de linha do final.
            if (root_directory->subdirectories_count > 0) {
                listDirectoryTree(root_directory, 0);
            }
            printf("Digite o numero do diretorio pai (0 para o diretorio raiz): ");
            scanf("%d", &parent_index);
            clearInputBuffer();

            // Adiciona o novo diretorio.
            addDirectory(directory_name, parent_index);
            break;
        }
        case 6:
            // Exibe todos os diretorios.
            printf("\nArvore de diretorios:\n");
            listDirectoryTree(root_directory, 0);
            break;
        case 7:
            printf("Informe o tamanho em setores da particao: ");
            scanf("%u", &partition_size);

            createFileSystemImage(partition_size);
            break;
        case 8:
            printf("Encerrando...\n");
            break;
        default:
            printf("Opcao invalida.\n");
            break;
        }
    } while (option != 8);
}

// Limpa o buffer de entrada.
void clearInputBuffer()
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

// Método que copia um arquivo do disco rígido para o sistema de arquivos.
void copyFileToFS(const char *source_file)
{
    // Abre o arquivo fonte.
    FILE *source = fopen(source_file, "rb");
    if (source == NULL)
    {
        printf("Erro ao abrir o arquivo fonte.\n");
        return;
    }

    // Verifica o tamanho do arquivo.
    fseek(source, 0, SEEK_END);
    long file_size = ftell(source);
    rewind(source);

    // Calcula o número de blocos necessarios.
    int required_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Procura blocos contíguos disponíveis no bitmap.
    int start_block = -1;
    for (int i = 0; i < boot_record.number_of_total_system_blocks - required_blocks + 1; i++)
    {
        int j;
        for (j = 0; j < required_blocks; j++)
        {
            if (bitmap.blocks[i + j] != 0)
                break;
        }
        if (j == required_blocks)
        {
            start_block = i;
            for (int k = 0; k < required_blocks; k++)
            {
                bitmap.blocks[start_block + k] = 1; // Marca os blocos como ocupados.
            }
            break;
        }
    }

    if (start_block == -1)
    {
        printf("Nao ha espaco contiguo disponivel no sistema de arquivos.\n");
        fclose(source);
        return;
    }

    // Copia o conteúdo do arquivo para a area de dados.
    fread(&data_area.data[start_block * BLOCK_SIZE], sizeof(unsigned char), file_size, source);

    fclose(source);

    // Adiciona uma entrada para o arquivo na tabela de diretorios.
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

    strcpy(table_entries[entry_index].entry_name, source_file);
    strcpy(table_entries[entry_index].entry_extension, "");
    table_entries[entry_index].status = 1;
    table_entries[entry_index].file_attribute = 1; // Marca como arquivo.
    table_entries[entry_index].first_block = start_block;
    table_entries[entry_index].size_in_bytes = file_size;

    // Encontra o diretório pai para adicionar o arquivo
    Directory *parent_dir = findDirectory(root_directory, 0);

    // Incrementa o contador de arquivos do diretório pai
    parent_dir->files_count++;

    // Adiciona o novo arquivo ao vetor de arquivos do diretório pai
    parent_dir->files = (Entries *)realloc(parent_dir->files, parent_dir->files_count * sizeof(Entries));
    parent_dir->files[parent_dir->files_count - 1] = table_entries[entry_index];

    printf("Arquivo copiado para o sistema de arquivos com sucesso.\n");

    int subDirIndex;
    printf("Digite o indice do subdiretorio destino (0 para raiz): ");
    scanf("%d", &subDirIndex);

    Directory *destDir = findDirectory(root_directory, subDirIndex);
    if (destDir == NULL)
    {
        printf("Subdiretorio destino não encontrado.\n");
        return;
    }

    // Incrementa o contador de arquivos do diretório destino
    destDir->files_count++;

    // Adiciona o novo arquivo ao vetor de arquivos do diretório destino
    destDir->files = (Entries *)realloc(destDir->files, destDir->files_count * sizeof(Entries));
    destDir->files[destDir->files_count - 1] = table_entries[entry_index];
}


// Método que copia um arquivo do sistema de arquivos para o disco rígido.
void copyFileFromFS(const char *entry_name)
{
    // Procura o arquivo na tabela de entradas.
    int entry_index = -1;
    for (int i = 0; i < ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries); i++)
    {
        if (table_entries[i].status != 0 && strcmp(table_entries[i].entry_name, entry_name) == 0)
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

    // Abre o arquivo destino no disco rígido.
    char dest_file[50];
    strcpy(dest_file, "copia_");
    strcat(dest_file, entry_name);

    FILE *dest = fopen(dest_file, "wb");
    if (dest == NULL)
    {
        printf("Erro ao abrir o arquivo de destino.\n");
        return;
    }

    // Escreve o conteúdo do arquivo na area de dados.
    fwrite(&data_area.data[table_entries[entry_index].first_block * BLOCK_SIZE], sizeof(unsigned char), table_entries[entry_index].size_in_bytes, dest);

    fclose(dest);

    printf("Arquivo copiado para o disco rigido com sucesso.\n");
}

// Método que lista os arquivos armazenados no sistema de arquivos.
void listFiles()
{
    printf("\nArquivos armazenados no sistema de arquivos:\n");

    for (int i = 0; i < ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries); i++)
    {
        if (table_entries[i].status == 1 && table_entries[i].file_attribute == 1)
        {
            printf("%s\n", table_entries[i].entry_name);
        }
    }
}

// Método que remove um arquivo do sistema de arquivos.
void deleteFile(const char *entry_name)
{
    // Procura o arquivo na tabela de entradas.
    int entry_index = -1;
    for (int i = 0; i < ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries); i++)
    {
        if (table_entries[i].status != 0 && strcmp(table_entries[i].entry_name, entry_name) == 0)
        {
            entry_index = i;
            break;
        }
    }

    if (entry_index == -1)
    {
        printf("Arquivo nao encontrado.\n");
        return;
    }

    // Libera os blocos de dados ocupados pelo arquivo no bitmap.
    int blocks_to_free = (table_entries[entry_index].size_in_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int i = 0; i < blocks_to_free; i++)
    {
        bitmap.blocks[table_entries[entry_index].first_block + i] = 0;
    }

    // Limpa a entrada da tabela de entradas.
    memset(&table_entries[entry_index], 0, sizeof(Entries));

    printf("Arquivo removido.\n");
}

// Encontra um diretorio com base no índice.
Directory *findDirectory(Directory *root, int index)
{
    if (index == 0)
        return root;
    if (root == NULL)
        return NULL;
    if (index <= root->files_count)
        return root;
    else
    {
        index -= root->files_count;
        for (int i = 0; i < root->subdirectories_count; i++)
        {
            Directory *subdirectory = root->subdirectories[i];
            if (index <= subdirectory->files_count)
                return subdirectory;
            index -= subdirectory->files_count;
            Directory *found = findDirectory(subdirectory, index);
            if (found != NULL)
                return found;
        }
    }
    return NULL;
}

// Adiciona um diretório ao sistema de arquivos.
void addDirectory(const char *directory_name, int parent_index)
{
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
    // Encontra o diretório pai
    Directory *parent_dir = findDirectory(root_directory, parent_index);
    if (parent_dir == NULL)
    {
        printf("Diretorio pai não encontrado.\n");
        return;
    }
    // Cria um novo diretório
     strcpy(table_entries[entry_index].entry_name, directory_name);
    strcpy(table_entries[entry_index].entry_extension, "");
    table_entries[entry_index].status = 1;
    table_entries[entry_index].file_attribute = 2; // Marca como diretorio.
    table_entries[entry_index].first_block = -1;
    table_entries[entry_index].size_in_bytes = 0;
    Directory *new_directory = (Directory *)malloc(sizeof(Directory));
    strcpy(new_directory->directory_name, directory_name);
    new_directory->files = NULL;
    new_directory->subdirectories = NULL;
    new_directory->index = parent_dir->subdirectories_count + parent_dir->files_count + 1; // O novo índice será o número atual de diretórios e arquivos mais um.
    new_directory->files_count = 0;
    new_directory->subdirectories_count = 0;
    new_directory->next = NULL;

    // Incrementa o contador de subdiretórios do diretório pai
    parent_dir->subdirectories_count++;
    // Realoca o vetor de subdiretórios do diretório pai
    parent_dir->subdirectories = (Directory **)realloc(parent_dir->subdirectories, parent_dir->subdirectories_count * sizeof(Directory *));
    parent_dir->subdirectories[parent_dir->subdirectories_count - 1] = new_directory;
}


// Método recursivo para listar a árvore de diretórios
void listDirectoryTree(Directory *root, int level)
{
    if (root != NULL)
    {
        for (int i = 0; i < level; i++)
        {
            printf("\t");
        }
        printf("%d - %s\n", root->index, root->directory_name);

        // Listar arquivos deste diretório
        for (int i = 0; i < root->files_count; i++)
        {
            for (int j = 0; j < level + 1; j++)
            {
                printf("\t");
            }
            printf("- %s\n", root->files[i].entry_name);
        }

        // Listar subdiretórios deste diretório
        for (int i = 0; i < root->subdirectories_count; i++)
        {
            listDirectoryTree(root->subdirectories[i], level + 1);
        }
    }
}

// Método que salva a imagem do sistema de arquivos.
void saveFileSystemImage(const char *isoName)
{
    FILE *file = fopen(isoName, "wb");
    if (file == NULL)
    {
        printf("Erro ao abrir o arquivo para salvar a imagem do sistema de arquivos.\n");
        return;
    }

    // Escreve o boot record.
    fwrite(&boot_record, sizeof(BootRecord), 1, file);

    // Escreve o bitmap.
    fwrite(bitmap.blocks, sizeof(unsigned char), boot_record.number_of_total_system_blocks, file);

    // Escreve a tabela de entradas.
    fwrite(table_entries, sizeof(Entries), ET_BLCK_COUNT * BLOCK_SIZE / sizeof(Entries), file);

    // Escreve a area de dados.
    fwrite(data_area.data, sizeof(unsigned char), (boot_record.number_of_data_section_blocks) * BLOCK_SIZE, file);

    fclose(file);

    printf("Imagem do sistema de arquivos salva com sucesso.\n");
}
