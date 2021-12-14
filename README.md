# DiskOracle - Ferramenta de Diagnóstico de Disco de Precisão

DiskOracle é um utilitário de linha de comando em C, leve e poderoso, projetado para fornecer uma análise profunda e precisa da saúde de dispositivos de armazenamento (HDDs e SSDs). Ele vai além dos relatórios de status superficiais, lendo e interpretando dados brutos diretamente do hardware.

Construído para ser rápido, portátil e sem dependências externas, o DiskOracle é a ferramenta ideal para administradores de sistemas, técnicos de TI e usuários avançados que precisam de um diagnóstico confiável.

## Recursos Principais

- **Análise SMART Detalhada (`--smart`):** Em vez de apenas mostrar o status geral "OK" ou "FAIL", o DiskOracle lê a tabela completa de atributos S.M.A.R.T. (Self-Monitoring, Analysis, and Reporting Technology) de um drive. Ele então analisa os valores brutos e os compara com os limiares definidos pelo fabricante para identificar sinais de alerta precoce de degradação do disco, mesmo que o status geral ainda seja "OK".

- **Teste de Superfície Completo (`--surface`):** Realiza uma varredura completa da superfície do disco para encontrar setores defeituosos (`bad sectors`). Oferece dois modos:
    - `quick`: Um teste rápido que lê uma amostra de setores em todo o disco para um diagnóstico veloz.
    - `deep`: Um teste exaustivo que lê cada setor do disco, garantindo a verificação mais completa possível. Fornece feedback de progresso em tempo real.

- **Listagem de Dispositivos (`--list`):** Detecta e lista todos os dispositivos de armazenamento físico conectados ao sistema, juntamente com seus índices e informações básicas, para que você saiba em qual alvo executar os diagnósticos.

- **Leve e Portátil:** Escrito inteiramente em C padrão, com o mínimo de dependências específicas do sistema operacional (abstraídas em uma camada PAL), o DiskOracle é compilado em um único executável pequeno e rápido que não requer instalação.

## Requisitos de Compilação

Para compilar o DiskOracle no Windows, você precisará de:
- **CMake:** Para gerar os arquivos de build.
- **MinGW-w64:** Um ambiente de compilação GCC para Windows.

## Como Compilar

1.  Clone ou baixe o código-fonte deste repositório.
2.  Crie uma pasta de build e navegue até ela:
    ```bash
    mkdir build_cmake
    cd build_cmake
    ```
3.  Execute o CMake para configurar o projeto:
    ```bash
    cmake .. -G "MinGW Makefiles"
    ```
4.  Compile o código usando o `mingw32-make`:
    ```bash
    mingw32-make
    ```
5.  Um executável `diskoracle.exe` será criado dentro da pasta `build_cmake`.

## Como Usar

:warning: **IMPORTANTE:** O DiskOracle precisa de acesso de baixo nível ao hardware. Por isso, você **DEVE** executá-lo a partir de um terminal (PowerShell ou CMD) com **privilégios de Administrador**.

### Sintaxe Básica
`diskoracle.exe [comando] [alvo] [opções]`

### Exemplos

**1. Listar todos os discos do sistema:**
```bash
.\diskoracle.exe --list
```
*Saída de Exemplo:*
```
Disk 0: WDC PC SN530 SDBPNPZ-512G-1114 (512 GB)
Disk 1: ST2000LM007-1R8174 (2000 GB)
```

**2. Obter um relatório SMART detalhado do Disco 1:**
```bash
.\diskoracle.exe --smart \\.\PHYSICALDRIVE1
```

**3. Realizar um teste de superfície rápido no Disco 1:**
```bash
.\diskoracle.exe --surface \\.\PHYSICALDRIVE1
```
*(O modo `quick` é o padrão se `--type` não for especificado)*

**4. Realizar um teste de superfície profundo e completo no Disco 0:**
```bash
.\diskoracle.exe --surface \\.\PHYSICALDRIVE0 --type deep
```

**5. Mostrar a tela de ajuda:**
```bash
.\diskoracle.exe --help
```

## Licença

Este projeto é licenciado sob a [Licença MIT](LICENSE.txt).
