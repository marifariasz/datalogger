import numpy as np
import matplotlib.pyplot as plt
import pandas as pd

path = "~/Downloads/medicoes_imu1(1).csv"

data = pd.read_csv(path, header=None)
df = pd.DataFrame(data)
plt.style.use('dark_background')
plt.rcParams['figure.figsize'] = (12, 6)

# Criando a figura com 2 subplots (1 linha, 2 colunas)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# Gráfico 1: Colunas 1, 3, 5 vs Coluna 0
ax1.plot(df[0], df[1], 'r-', label='Coluna 1')
ax1.plot(df[0], df[3], 'g--', label='Coluna 3')
ax1.plot(df[0], df[5], 'b:', label='Coluna 5')
ax1.set_xlabel('Coluna de Entrada (0)')
ax1.set_ylabel('Valores das Colunas')
ax1.set_title('Colunas 1, 3, 5 vs Entrada')
ax1.legend()
ax1.grid(False, alpha=0.3)

# Gráfico 2: Colunas 2, 4, 6 vs Coluna 0
ax2.plot(df[0], df[2], 'c-', label='Coluna 2')
ax2.plot(df[0], df[4], 'm--', label='Coluna 4')
ax2.plot(df[0], df[6], 'y:', label='Coluna 6')
ax2.set_xlabel('Coluna de Entrada (0)')
ax2.set_ylabel('Valores das Colunas')
ax2.set_title('Colunas 2, 4, 6 vs Entrada')
ax2.legend()
ax2.grid(False, alpha=0.3)

# Ajusta o layout para evitar sobreposição
plt.tight_layout()
plt.show()