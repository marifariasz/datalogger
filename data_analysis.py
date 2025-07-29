# Importar bibliotecas necessárias
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from google.colab import files
import warnings

# Configurações visuais
plt.style.use('seaborn-v0_8')
warnings.filterwarnings('ignore')
plt.rcParams['figure.figsize'] = [12, 8]
plt.rcParams['font.size'] = 10

print("Bibliotecas carregadas com sucesso.")
print("Pronto para análise de dados do MPU6050.")

# ====================================================================
# CÉLULA 2: UPLOAD E CARREGAMENTO DOS DADOS
# ====================================================================

# Upload do arquivo CSV
print("\nPor favor, selecione o arquivo CSV gerado pelo seu dispositivo:")
uploaded = files.upload()

# Verificar se o arquivo foi carregado
if uploaded:
    filename = list(uploaded.keys())[0]
    print(f"Arquivo '{filename}' carregado com sucesso.")

    # Carregar dados
    try:
        df = pd.read_csv(filename)

        # --- DEBUG: Imprimir os nomes das colunas exatamente como foram lidos ---
        print(f"\nColunas detectadas no arquivo: {list(df.columns)}")

        # --- CORREÇÃO DEFINITIVA ---
        # Mapear os nomes EXATOS do seu CSV (com espaços) para nomes limpos
        # que usaremos no resto do script.
        colunas_para_renomear = {
            'Amostra': 'Amostra',
            ' Aceleração X': 'Acel-X',
            ' Aceleração Y': 'Acel-Y',
            ' Aceleração Z': 'Acel-Z',
            ' Giroscópio X': 'Giro-X',
            ' Giroscópio Y': 'Giro-Y',
            ' Giroscópio Z': 'Giro-Z',
            ' Tempo (s)': 'Tempo_s'
        }
        df.rename(columns=colunas_para_renomear, inplace=True)
        
        # Verificar se a coluna de tempo existe após renomear
        if 'Tempo_s' not in df.columns:
            print("ERRO CRÍTICO: A coluna de tempo não foi encontrada mesmo após a tentativa de correção. Verifique o cabeçalho do seu CSV.")
            df = None

        if df is not None:
            # Informações básicas
            print("\nINFORMAÇÕES GERAIS DOS DADOS:")
            print(f"  - Total de amostras: {len(df)}")
            print(f"  - Duração da gravação: {df['Tempo_s'].max():.2f} segundos")
            print(f"  - Taxa de amostragem média: {len(df) / df['Tempo_s'].max():.1f} Hz")
            print(f"  - Colunas após limpeza: {list(df.columns)}")

            # Mostrar as primeiras linhas dos dados
            print("\nAMOSTRA DOS DADOS (PRIMEIRAS 5 LINHAS):")
            print(df.head())

    except Exception as e:
        print(f"Erro ao carregar ou processar o arquivo: {e}")
        df = None
else:
    print("Nenhum arquivo foi carregado. A análise não pode continuar.")
    df = None

# ====================================================================
# CÉLULA 3: GRÁFICOS PRINCIPAIS (ACELERAÇÃO E GIROSCÓPIO)
# ====================================================================

if df is not None:
    # Criar gráficos principais em uma figura com duas subtramas
    fig, axes = plt.subplots(2, 1, figsize=(15, 10), sharex=True)
    fig.suptitle('Análise de Dados do Sensor MPU6050', fontsize=18, fontweight='bold')

    # Usar os nomes de coluna LIMPOS e RENOMEADOS
    # Gráfico 1: ACELERAÇÃO
    axes[0].plot(df['Tempo_s'], df['Acel-X'], label='Aceleração X', color='red', linewidth=1.5, alpha=0.8)
    axes[0].plot(df['Tempo_s'], df['Acel-Y'], label='Aceleração Y', color='green', linewidth=1.5, alpha=0.8)
    axes[0].plot(df['Tempo_s'], df['Acel-Z'], label='Aceleração Z', color='blue', linewidth=1.5, alpha=0.8)

    axes[0].set_title('Leituras de Aceleração', fontsize=14, pad=15)
    axes[0].set_ylabel('Aceleração (g)', fontsize=12)
    axes[0].legend(fontsize=11)
    axes[0].grid(True, linestyle='--', alpha=0.5)
    axes[0].set_xlim(df['Tempo_s'].min(), df['Tempo_s'].max())

    # Gráfico 2: GIROSCÓPIO
    axes[1].plot(df['Tempo_s'], df['Giro-X'], label='Giroscópio X', color='red', linewidth=1.5, alpha=0.8)
    axes[1].plot(df['Tempo_s'], df['Giro-Y'], label='Giroscópio Y', color='green', linewidth=1.5, alpha=0.8)
    axes[1].plot(df['Tempo_s'], df['Giro-Z'], label='Giroscópio Z', color='blue', linewidth=1.5, alpha=0.8)

    axes[1].set_title('Leituras de Giroscópio', fontsize=14, pad=15)
    axes[1].set_xlabel('Tempo (s)', fontsize=12)
    axes[1].set_ylabel('Velocidade Angular (°/s)', fontsize=12)
    axes[1].legend(fontsize=11)
    axes[1].grid(True, linestyle='--', alpha=0.5)

    plt.tight_layout(rect=[0, 0, 1, 0.96]) # Ajusta o layout para não sobrepor o supertítulo
    plt.show()

    # Estatísticas rápidas
    print("\nESTATÍSTICAS DESCRITIVAS:")
    print("--------------------------------------------------")
    print("ACELERAÇÃO (g): Média e Desvio Padrão")
    print(f"  - Eixo X: {df['Acel-X'].mean():.3f} ± {df['Acel-X'].std():.3f}")
    print(f"  - Eixo Y: {df['Acel-Y'].mean():.3f} ± {df['Acel-Y'].std():.3f}")
    print(f"  - Eixo Z: {df['Acel-Z'].mean():.3f} ± {df['Acel-Z'].std():.3f}")

    print("\nGIROSCÓPIO (°/s): Média e Desvio Padrão")
    print(f"  - Eixo X: {df['Giro-X'].mean():.2f} ± {df['Giro-X'].std():.2f}")
    print(f"  - Eixo Y: {df['Giro-Y'].mean():.2f} ± {df['Giro-Y'].std():.2f}")
    print(f"  - Eixo Z: {df['Giro-Z'].mean():.2f} ± {df['Giro-Z'].std():.2f}")

    print("\nAnálise concluída com sucesso.")

else:
    print("\nNão foi possível gerar os gráficos, pois os dados não foram carregados corretamente.")
