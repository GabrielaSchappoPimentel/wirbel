import os
import struct

import pygame

EIXO_DIRECAO = 0      # analogico esquerdo, horizontal
EIXO_LT = 2
EIXO_RT = 5
ZONA_MORTA_DIRECAO = 0.10
ZONA_MORTA_GATILHO = 0.03
MAGIC_CMD = 0xA5
FORMATO_CMD = "<BHBbB"   # magic, seq, throttle, steer, brake = 6 bytes
HZ = 50


def zona_morta(v, dz):
    """Zera valores pequenos e reescala o resto para continuar indo ate +-1."""
    if abs(v) < dz:
        return 0.0
    sinal = 1 if v > 0 else -1
    return sinal * (abs(v) - dz) / (1 - dz)


def gatilho_para_pct(v):
    """Gatilho vai de -1 (solto) a +1 (apertado) -> 0 a 100."""
    pct = (v + 1) / 2
    if pct < ZONA_MORTA_GATILHO:
        return 0
    return min(100, round(pct * 100))


def montar_pacote(seq, throttle, steer, brake):
    return struct.pack(FORMATO_CMD, MAGIC_CMD, seq & 0xFFFF, throttle, steer, brake)


def barra(tela, fonte, rotulo, valor, minimo, maximo, y):
    """Desenha uma barra horizontal com o valor atual."""
    x, larg, alt = 150, 400, 28
    pygame.draw.rect(tela, (60, 60, 60), (x, y, larg, alt))
    frac = (valor - minimo) / (maximo - minimo)
    pygame.draw.rect(tela, (80, 180, 90), (x, y, int(larg * frac), alt))
    tela.blit(fonte.render(f"{rotulo}: {valor}", True, (230, 230, 230)), (10, y + 4))


def main():
    os.environ["SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"] = "1"
    pygame.init()
    pygame.joystick.init()
    tela = pygame.display.set_mode((600, 260))
    pygame.display.set_caption("Wirbel - painel de controle")
    fonte = pygame.font.SysFont("monospace", 16)
    relogio = pygame.time.Clock()

    controle = None
    seq = 0
    rodando = True

    while rodando:
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                rodando = False
            elif ev.type == pygame.JOYDEVICEADDED and controle is None:
                controle = pygame.joystick.Joystick(ev.device_index)
            elif ev.type == pygame.JOYDEVICEREMOVED:
                controle = None

        if controle is not None:
            throttle = gatilho_para_pct(controle.get_axis(EIXO_RT))
            brake = gatilho_para_pct(controle.get_axis(EIXO_LT))
            steer = round(zona_morta(controle.get_axis(EIXO_DIRECAO), ZONA_MORTA_DIRECAO) * 100)
        else:
            # Sem controle: acelerador zero e freio total
            throttle, steer, brake = 0, 0, 100

        pacote = montar_pacote(seq, throttle, steer, brake)
        seq += 1

        tela.fill((25, 25, 30))
        barra(tela, fonte, "Acelerador", throttle, 0, 100, 20)
        barra(tela, fonte, "Freio", brake, 0, 100, 60)
        barra(tela, fonte, "Direcao", steer, -100, 100, 100)
        estado = "CONECTADO" if controle else "SEM CONTROLE"
        tela.blit(fonte.render(f"Controle: {estado}", True, (230, 230, 230)), (10, 150))
        tela.blit(fonte.render(f"Pacote ({len(pacote)} bytes): {pacote.hex(' ')}",
                               True, (230, 230, 230)), (10, 180))
        pygame.display.flip()

        relogio.tick(HZ)

    pygame.quit()


if __name__ == "__main__":
    main()