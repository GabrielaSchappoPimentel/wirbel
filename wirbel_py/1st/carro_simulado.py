# carro_simulado.py
# Simula o firmware: recebe comandos UDP, aplica as regras de seguranca e
# responde com telemetria. A logica daqui e a especificacao do ESP32.
import random
import socket
import struct
import time

PORTA = 4210
PERDA = 0.0   # 0.3 = descarta 30% dos pacotes recebidos (teste de link ruim)

MAGIC_CMD = 0xA5
MAGIC_TEL = 0x5A
FORMATO_CMD = "<BHBbB"      # magic, seq, throttle, steer, brake (6 bytes)
FORMATO_TEL = "<BHBHBbBB"   # magic, seq_eco, estado, vbat_mv, throttle, steer, brake, rx_por_s (10 bytes)
TAM_CMD = struct.calcsize(FORMATO_CMD)

FAILSAFE_S = 0.200
TEL_PERIODO_S = 0.100
VBAT_CHEIA_MV = 8400.0
VBAT_BAIXA_MV = 6600
VBAT_CORTE_MV = 6200

SEM_LINK, DESARMADO, ARMADO, BAT_BAIXA, CORTE = range(5)
NOMES = ["SEM_LINK", "DESARMADO", "ARMADO", "BAT_BAIXA", "CORTE"]


def seq_novo(seq, ultimo):
    """True se seq e mais novo que o ultimo (trata a volta do contador em 65535)."""
    if ultimo is None:
        return True
    delta = (seq - ultimo) & 0xFFFF
    return 0 < delta < 0x8000


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PORTA))
    sock.settimeout(0.01)
    print(f"Carro simulado na porta {PORTA}. Ctrl+C para sair.")

    cliente = None        # (ip, porta) de quem mandou o ultimo comando
    ultimo_seq = None
    eco = 0               # ultimo seq aceito, devolvido na telemetria
    ultimo_rx = 0.0
    cmd = (0, 0, 100)     # throttle, steer, brake pedidos
    armado = False
    corte = False
    vbat = VBAT_CHEIA_MV
    rx_contagem = 0
    rx_por_s = 0
    aplicado = (0, 0, 100)
    t_janela = t_anterior = time.monotonic()
    t_tel = 0.0
    estado_anterior = None

    while True:
        agora = time.monotonic()
        dt = agora - t_anterior
        t_anterior = agora

        # ---- 1. Receber ----
        try:
            dados, origem = sock.recvfrom(64)
        except socket.timeout:
            dados = None

        if dados is not None and len(dados) == TAM_CMD and random.random() >= PERDA:
            magic, seq, t, s, b = struct.unpack(FORMATO_CMD, dados)
            if magic == MAGIC_CMD and seq_novo(seq, ultimo_seq):
                ultimo_seq = eco = seq
                ultimo_rx = agora
                cliente = origem
                rx_contagem += 1
                cmd = (min(t, 100), max(-100, min(100, s)), min(b, 100))
                if cmd[0] == 0:
                    armado = True   # so arma com acelerador solto

        # ---- 2. Link e failsafe ----
        link_ok = (agora - ultimo_rx) < FAILSAFE_S
        if not link_ok:
            armado = False       # exige acelerador em zero para rearmar
            ultimo_seq = None    # aceita seq reiniciado se o app do PC reabrir

        # ---- 3. Estado e valores aplicados ----
        thr, steer, brk = cmd
        if brk > 0:
            thr = 0              # freio tem prioridade sobre o acelerador
        if not link_ok:
            estado, aplicado = SEM_LINK, (0, 0, 100)
        elif corte:
            estado, aplicado = CORTE, (0, 0, 100)
        elif not armado:
            estado, aplicado = DESARMADO, (0, steer, brk)
        elif vbat < VBAT_BAIXA_MV:
            estado, aplicado = BAT_BAIXA, (thr // 2, steer, brk)
        else:
            estado, aplicado = ARMADO, (thr, steer, brk)

        # ---- 4. Bateria simulada (cai mais rapido com acelerador) ----
        vbat -= (2.0 + aplicado[0] * 2.0) * dt   # mV por segundo
        if vbat < VBAT_CORTE_MV:
            corte = True         # trava ate reiniciar

        # ---- 5. Janela de 1 s para contar pacotes recebidos ----
        if agora - t_janela >= 1.0:
            rx_por_s, rx_contagem, t_janela = rx_contagem, 0, agora

        # ---- 6. Telemetria a 10 Hz ----
        if cliente is not None and agora - t_tel >= TEL_PERIODO_S:
            t_tel = agora
            pacote = struct.pack(FORMATO_TEL, MAGIC_TEL, eco, estado, int(vbat),
                                 aplicado[0], aplicado[1], aplicado[2],
                                 min(rx_por_s, 255))
            sock.sendto(pacote, cliente)

        if estado != estado_anterior:
            print(f"Estado: {NOMES[estado]}  (vbat {vbat / 1000:.2f} V)")
            estado_anterior = estado


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nEncerrado.")