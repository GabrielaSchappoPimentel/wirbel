# passo3_painel.py
import os
import socket
import struct
import time

import pygame

IP_CARRO = "192.168.4.1"
PORTA = 4210

EIXO_DIRECAO = 0
EIXO_LT = 2
EIXO_RT = 5
ZONA_MORTA_DIRECAO = 0.10
ZONA_MORTA_GATILHO = 0.03

MAGIC_CMD = 0xA5
MAGIC_TEL = 0x5A
FORMATO_CMD = "<BHBbB"
FORMATO_TEL = "<BHBHBbBB"
TAM_TEL = struct.calcsize(FORMATO_TEL)
HZ = 50
TIMEOUT_TEL_S = 0.5

VERDE = (80, 180, 90)
AMARELO = (230, 180, 60)
LARANJA = (230, 140, 40)
VERMELHO = (220, 70, 70)
ESTADOS = {
    0: ("SEM LINK", VERMELHO),
    1: ("DESARMADO", AMARELO),
    2: ("ARMADO", VERDE),
    3: ("BATERIA BAIXA", LARANJA),
    4: ("BATERIA CORTADA", VERMELHO),
}


def zona_morta(v, dz):
    if abs(v) < dz:
        return 0.0
    sinal = 1 if v > 0 else -1
    return sinal * (abs(v) - dz) / (1 - dz)


def gatilho_para_pct(v):
    pct = (v + 1) / 2
    if pct < ZONA_MORTA_GATILHO:
        return 0
    return min(100, round(pct * 100))


def montar_pacote(seq, throttle, steer, brake):
    return struct.pack(FORMATO_CMD, MAGIC_CMD, seq & 0xFFFF, throttle, steer, brake)


def ler_telemetria(sock, enviados):
    """Esvazia o socket. Devolve (telemetria_mais_recente, atraso_ms) ou (None, None)."""
    tel, atraso = None, None
    while True:
        try:
            dados, _ = sock.recvfrom(64)
        except BlockingIOError:
            return tel, atraso
        if len(dados) != TAM_TEL or dados[0] != MAGIC_TEL:
            continue
        _, eco, estado, vbat_mv, thr, steer, brk, rx = struct.unpack(FORMATO_TEL, dados)
        tel = {"estado": estado, "vbat_mv": vbat_mv, "thr": thr,
               "steer": steer, "brk": brk, "rx": rx}
        t_envio = enviados.get(eco)
        if t_envio is not None:
            atraso = (time.monotonic() - t_envio) * 1000


def barra(tela, fonte, rotulo, valor, minimo, maximo, y, cor=VERDE, texto=None):
    x, larg, alt = 170, 400, 24
    pygame.draw.rect(tela, (60, 60, 60), (x, y, larg, alt))
    frac = max(0.0, min(1.0, (valor - minimo) / (maximo - minimo)))
    pygame.draw.rect(tela, cor, (x, y, int(larg * frac), alt))
    texto = valor if texto is None else texto
    tela.blit(fonte.render(f"{rotulo}: {texto}", True, (230, 230, 230)), (10, y + 3))


def linha(tela, fonte, texto, y):
    tela.blit(fonte.render(texto, True, (230, 230, 230)), (10, y))


def main():
    os.environ["SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"] = "1"
    pygame.init()
    pygame.joystick.init()
    tela = pygame.display.set_mode((600, 360))
    pygame.display.set_caption("Wirbel - painel de controle")
    fonte = pygame.font.SysFont("monospace", 16)
    fonte_g = pygame.font.SysFont("monospace", 22, bold=True)
    relogio = pygame.time.Clock()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    destino = (IP_CARRO, PORTA)
    enviados = {}   # seq -> instante do envio (para medir o atraso)

    controle = None
    seq = 0
    tel = None
    atraso_ms = None
    t_ultima_tel = 0.0
    erro_rede = False
    rodando = True

    while rodando:
        agora = time.monotonic()

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
            steer = round(zona_morta(controle.get_axis(EIXO_DIRECAO),
                                     ZONA_MORTA_DIRECAO) * 100)
        else:
            throttle, steer, brake = 0, 0, 100

        # ---- Enviar ----
        pacote = montar_pacote(seq, throttle, steer, brake)
        try:
            sock.sendto(pacote, destino)
            erro_rede = False
        except OSError:          # ex.: notebook fora do Wi-Fi do carro
            erro_rede = True
        enviados[seq & 0xFFFF] = agora
        enviados.pop((seq - 200) & 0xFFFF, None)   # guarda so ~4 s de historico
        seq += 1

        # ---- Receber ----
        novo, atraso = ler_telemetria(sock, enviados)
        if novo is not None:
            tel, t_ultima_tel = novo, agora
        if atraso is not None:
            atraso_ms = atraso
        tel_ok = tel is not None and (agora - t_ultima_tel) < TIMEOUT_TEL_S

        # ---- Desenhar ----
        tela.fill((25, 25, 30))
        barra(tela, fonte, "Acelerador", throttle, 0, 100, 15)
        barra(tela, fonte, "Freio", brake, 0, 100, 45)
        barra(tela, fonte, "Direcao", steer, -100, 100, 75)

        if erro_rede:
            nome, cor = "SEM REDE", VERMELHO
        elif not tel_ok:
            nome, cor = "SEM TELEMETRIA", VERMELHO
        else:
            nome, cor = ESTADOS.get(tel["estado"], ("?", AMARELO))
        pygame.draw.rect(tela, cor, (10, 115, 580, 40))
        tela.blit(fonte_g.render(nome, True, (20, 20, 20)), (20, 124))

        if tel_ok:
            vbat = tel["vbat_mv"]
            cor_bat = VERDE if vbat >= 6600 else (LARANJA if vbat >= 6200 else VERMELHO)
            barra(tela, fonte, "Bateria", vbat, 6000, 8400, 170, cor_bat,
                  f"{vbat / 1000:.2f} V")
            linha(tela, fonte, f"Carro aplicou: acel {tel['thr']}  "
                               f"freio {tel['brk']}  dir {tel['steer']:+d}", 210)
            perda = max(0, 100 - tel["rx"] * 100 / HZ)
            linha(tela, fonte, f"Link: {tel['rx']}/{HZ} pacotes/s  (perda {perda:.0f}%)", 235)
            atraso_txt = "--" if atraso_ms is None else f"{atraso_ms:.0f} ms"
            linha(tela, fonte, f"Atraso do eco: {atraso_txt}", 260)
        linha(tela, fonte, "Controle: " + ("CONECTADO" if controle else "SEM CONTROLE"), 295)
        linha(tela, fonte, f"Pacote: {pacote.hex(' ')}", 320)
        pygame.display.flip()

        relogio.tick(HZ)

    # Ao fechar, manda 3 pacotes "acelerador 0, freio total" antes de sair
    for i in range(3):
        try:
            sock.sendto(montar_pacote(seq + i, 0, 0, 100), destino)
        except OSError:
            pass
    pygame.quit()


if __name__ == "__main__":
    main()