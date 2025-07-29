# 📊 Pico IMU Data Logger

Este projeto transforma um **Raspberry Pi Pico** em um poderoso e portátil **Data Logger** para a unidade de medição inercial (**IMU MPU6050**). Ele captura dados de aceleração e giroscópio, salvando-os em um cartão SD em formato `.csv` para análise posterior.

O dispositivo possui uma interface de usuário completa com um display **OLED**, botões para navegação, um **LED RGB** para feedback visual do status e um **buzzer** para alertas sonoros. Além disso, oferece uma interface de linha de comando (CLI) via terminal serial para controle avançado.

***

## ✨ Funcionalidades

* **📈 Coleta de Dados:** Captura dados de 6 eixos do MPU6050 (Acelerômetro 3-eixos, Giroscópio 3-eixos).
* **💾 Armazenamento em Cartão SD:** Salva as medições em arquivos `.csv` numerados sequencialmente (ex: `medicoes_imu1.csv`, `medicoes_imu2.csv`).
* **📺 Interface com Display OLED:** Um menu interativo de 3 páginas mostra o status do sistema, dados do SD e leituras do sensor em tempo real.
* **🔘 Controle por Botões:**
    * **Botão A:** Navega entre as páginas do menu.
    * **Botão B:** Reinicia o Pico em modo bootloader (para fácil reprogramação).
* **💻 CLI via Terminal Serial:** Permite controle total sobre o dispositivo, incluindo formatação e montagem do SD, listagem de arquivos e início da captura.
* **🚥 Indicador de Status com LED RGB:** Fornece feedback visual claro sobre o estado atual do sistema.
* **🔊 Alertas Sonoros:** Um buzzer emite sons para indicar o início/fim da gravação e condições de erro.
* **🐍 Script de Análise em Python:** Inclui um script pronto para ser usado no Google Colab para carregar, visualizar e analisar os dados do arquivo `.csv` gerado.

***

## 🛠️ Hardware Necessário

* 1x **Raspberry Pi Pico**
* 1x **Sensor IMU MPU6050**
* 1x **Display OLED SSD1306** (128x64, I2C)
* 1x **Módulo para Cartão MicroSD** (SPI)
* 1x **LED RGB** (Cátodo Comum)
* 1x **Buzzer Ativo/Passivo**
* 2x **Botões de Pressão (Push Buttons)**
* Resistores (conforme necessário para os LEDs e pull-ups)
* Protoboard e fios de jumper

***

## 🔌 Pinagem e Conexões

| Componente            | Pino do Pico | Descrição                  |
| --------------------- | ------------ | -------------------------- |
| **MPU6050 (I2C0)** | GP0 (SDA)    | I2C0 Data                  |
|                       | GP1 (SCL)    | I2C0 Clock                 |
| **Display OLED (I2C1)**| GP14 (SDA)   | I2C1 Data                  |
|                       | GP15 (SCL)   | I2C1 Clock                 |
| **LED RGB** | GP11         | LED Verde (Green)          |
|                       | GP12         | LED Azul (Blue)            |
|                       | GP13         | LED Vermelho (Red)         |
| **Botões** | GP5          | Botão A (Navegar Menu)     |
|                       | GP6          | Botão B (Reset Bootloader) |
| **Buzzer** | **(Definir)**| Conectar a um pino PWM     |
| **Módulo SD (SPI)** | GP16 (TX)    | SPI0 TX -> MOSI            |
|                       | GP17 (CSn)   | SPI0 CSn -> Chip Select    |
|                       | GP18 (SCK)   | SPI0 SCK -> Clock          |
|                       | GP19 (RX)    | SPI0 RX <- MISO            |
| *Alimentação* | 3V3 (OUT)    | Alimentação para sensores  |
|                       | GND          | Terra Comum                |

***

## 🚀 Como Usar

### 1. Interação Física

* **Botão A:** Pressione para alternar entre as 3 páginas do menu no display OLED.
* **Botão B:** Pressione para reiniciar o Pico em modo bootloader, facilitando o upload de um novo firmware.

### 2. Interface de Linha de Comando (CLI)

Conecte o Pico ao seu computador via USB e abra um terminal serial (ex: PuTTY, Thonny, VS Code Serial Monitor) com a porta COM correta. Você verá uma lista de comandos disponíveis.

| Comando (Tecla) | Ação                                    |
| :-------------: | --------------------------------------- |
|       `a`       | **Monta** o cartão SD.                  |
|       `b`       | **Desmonta** o cartão SD.               |
|       `c`       | **Lista** os arquivos no cartão SD.     |
|       `d`       | **Mostra o conteúdo** do último arquivo. |
|       `e`       | Mostra o **espaço livre** no cartão SD. |
|       `f`       | **Inicia a captura** dos dados do IMU.  |
|       `g`       | **Formata** o cartão SD (CUIDADO!).      |
|       `h`       | Mostra a lista de **ajuda** novamente.  |

***

## 🚥 Tabela de Cores do LED de Status

O LED RGB fornece um feedback rápido sobre o que o dispositivo está fazendo.

| Cor do LED          | Estado do Sistema                               |
| ------------------- | ----------------------------------------------- |
| **🟡 Amarelo** | Inicializando ou montando o cartão SD.          |
| **🟢 Verde** | Sistema pronto e aguardando um comando.         |
| **🔴 Vermelho** | Captura de dados do IMU em andamento.           |
| **🔵 Azul (Piscando)** | Acessando o cartão SD (leitura ou gravação).  |
| **🟣 Roxo (Piscando)** | **ERRO!** (Ex: Falha ao montar o SD, falha de escrita). |

***

## 📈 Análise dos Dados

O arquivo `.csv` gerado contém as seguintes colunas:
`Amostra,Acel-X,Acel-Y,Acel-Z,Giro-X,Giro-Y,Giro-Z,Tempo(s)`

Use o script Python `analise_dados.py` em um ambiente como o **Google Colab** ou **Jupyter Notebook** para facilmente fazer o upload do arquivo e gerar gráficos detalhados das leituras do acelerômetro e do giroscópio.

***

## ✍️ Desenvolvido Por

* **Mariana Farias da Silva**
