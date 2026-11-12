# passo1_controle.py
# Objetivo: descobrir como o Linux numera os eixos e botoes do controle.
# Executar:  python passo1_controle.py      (sair com Ctrl+C)

import os

# Precisa vir ANTES de importar o pygame.
# Sem isso, o SDL ignora o controle quando a janela do app perde o foco
# (ex.: voce clica em outro programa) -> no futuro, o carro pararia.
os.environ["SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"] = "1"

import pygame

pygame.init()
pygame.joystick.init()
relogio = pygame.time.Clock()

# ---- 1. Esperar um controle aparecer ----
print("Procurando controle... (ligue pelo cabo USB)")
while pygame.joystick.get_count() == 0:
    pygame.event.pump()   # deixa o pygame processar eventos do sistema
    relogio.tick(10)      # espera ate completar 1/10 s

controle = pygame.joystick.Joystick(0)   # primeiro controle encontrado
print(f"Conectado: {controle.get_name()}")
print(f"Eixos: {controle.get_numaxes()}   Botoes: {controle.get_numbuttons()}")
print("Mexa um comando por vez e anote o numero que muda.\n")

# ---- 2. Ler e mostrar tudo, 50 vezes por segundo ----
try:
    rodando = True
    while rodando:
        # A fila de eventos precisa ser esvaziada a cada ciclo;
        # e assim que o pygame atualiza os valores do controle.
        for evento in pygame.event.get():
            if evento.type == pygame.JOYDEVICEREMOVED:
                print("\nControle desconectado!")
                rodando = False

        eixos = [controle.get_axis(i) for i in range(controle.get_numaxes())]
        botoes = [controle.get_button(i) for i in range(controle.get_numbuttons())]
        hats = [controle.get_hat(i) for i in range(controle.get_numhats())]

        texto_eixos = " ".join(f"{i}:{v:+.2f}" for i, v in enumerate(eixos))
        texto_botoes = "".join(str(b) for b in botoes)

        # \r volta ao inicio da linha: a mesma linha e reescrita (sem rolar a tela)
        print(f"\r{texto_eixos} | {texto_botoes} | hats:{hats}   ", end="", flush=True)
        
        relogio.tick(50)   # trava o loop em 50 Hz (20 ms por volta)

except KeyboardInterrupt:
    print("\nEncerrado.")
finally:
    pygame.quit()