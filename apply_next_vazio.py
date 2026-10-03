#!/usr/bin/env python3
"""
apply_next_vazio.py

Faz o core aceitar um arquivo "falso" com extensao .next (ex.: Next.next)
como pedido para ligar o Next vazio (boot do NextZXOS), sem tentar carrega-lo
como jogo.

Uso: python apply_next_vazio.py [caminho\\zesarux_glue.c]
Se nao passar o caminho, procura zesarux_glue.c na pasta atual e subpastas.

Valida tudo em memoria. Se qualquer ponto nao bater, aborta sem alterar nada.
Antes de gravar, cria zesarux_glue.c.bak_next_vazio.
"""
import sys, os, shutil

def abortar(msg):
    print("ABORTADO (nada foi alterado): " + msg)
    sys.exit(1)

def achar_glue():
    """Sem argumento: procura zesarux_glue.c na pasta atual e nas subpastas,
    ignorando copias antigas (_removed*, _debug, obj-win, dist)."""
    ignorar = ("_removed", "_debug", "obj-win", "dist")
    achados = []
    for raiz, pastas, arquivos in os.walk("."):
        pastas[:] = [d for d in pastas if not d.startswith(ignorar)]
        if "zesarux_glue.c" in arquivos:
            achados.append(os.path.join(raiz, "zesarux_glue.c"))
    if len(achados) == 1:
        return achados[0]
    if not achados:
        abortar("zesarux_glue.c nao encontrado nesta pasta nem nas subpastas. "
                "Rode o script na pasta next ou passe o caminho do arquivo.")
    abortar("achei mais de um zesarux_glue.c; passe o caminho do certo:\n  "
            + "\n  ".join(achados))

path = sys.argv[1] if len(sys.argv) > 1 else achar_glue()
print("Arquivo: " + path)

if not os.path.isfile(path):
    abortar("arquivo nao encontrado: " + path)

with open(path, "rb") as f:
    dados = f.read()

try:
    texto = dados.decode("utf-8")
except UnicodeDecodeError:
    texto = dados.decode("latin-1")
    codificacao = "latin-1"
else:
    codificacao = "utf-8"

if "zx_is_dummy_next" in texto:
    abortar("o patch ja parece aplicado (zx_is_dummy_next ja existe).")

# Preserva o estilo de quebra de linha do arquivo (CRLF ou LF).
nl = "\r\n" if "\r\n" in texto else "\n"

ancora_funcao = "bool zx_load_content(const char *path)" + nl + "{" + nl
ancora_if = ("    /* Sem caminho: boot do NextZXOS / BASIC, como o ZEsarUX faz com --noargs. */"
             + nl + "    if (path && path[0])" + nl)

if texto.count(ancora_funcao) != 1:
    abortar("trecho de zx_load_content nao encontrado exatamente 1 vez (achei %d)."
            % texto.count(ancora_funcao))
if texto.count(ancora_if) != 1:
    abortar("trecho 'if (path && path[0])' nao encontrado exatamente 1 vez (achei %d)."
            % texto.count(ancora_if))
if texto.index(ancora_funcao) > texto.index(ancora_if):
    abortar("ordem inesperada dos trechos em zx_load_content.")

helper = nl.join([
    "/* Arquivo \"falso\" usado so para o menu do Batocera ter um item que lance o",
    " * Next vazio (ex.: roms/next/Next.next). A extensao .next nao e' conteudo de",
    " * verdade: nao ha o que passar para o quickload, entao tratamos como boot",
    " * sem midia, igual a path vazio. */",
    "static bool zx_is_dummy_next(const char *path)",
    "{",
    "    const char *p, *dot = NULL;",
    "",
    "    for (p = path; *p; p++)",
    "    {",
    "        if (*p == '/' || *p == '\\\\')",
    "            dot = NULL;",
    "        else if (*p == '.')",
    "            dot = p;",
    "    }",
    "",
    "    return dot",
    "        && (dot[1] == 'n' || dot[1] == 'N')",
    "        && (dot[2] == 'e' || dot[2] == 'E')",
    "        && (dot[3] == 'x' || dot[3] == 'X')",
    "        && (dot[4] == 't' || dot[4] == 'T')",
    "        && dot[5] == '\\0';",
    "}",
    "",
    "",
])

novo_if = ancora_if.replace("if (path && path[0])",
                            "if (path && path[0] && !zx_is_dummy_next(path))")

novo = texto.replace(ancora_funcao, helper + ancora_funcao, 1)
novo = novo.replace(ancora_if, novo_if, 1)

# Conferencias finais em memoria.
if novo.count("zx_is_dummy_next") != 2 + 0 and novo.count("zx_is_dummy_next") != 3:
    abortar("contagem inesperada apos a edicao.")
if "!zx_is_dummy_next(path))" not in novo:
    abortar("a condicao nova nao entrou.")

shutil.copyfile(path, path + ".bak_next_vazio")
with open(path, "wb") as f:
    f.write(novo.encode(codificacao))

print("OK: patch aplicado em " + path)
print("Backup: " + path + ".bak_next_vazio")
