# C HTTP Server - macOS System Monitor

Tento projekt je semestrální prací pro předmět **[4IZ110](https://4iz110.vse.cz)** na [VŠE](https://vse.cz). 

Jedná se o implementaci webového serveru v Cčku, který v real-time(u) monitoruje a zobrazuje využití systémových prostředků (CPU, RAM, etc,...) macOS.

![showcase](README/showcase.gif)

------

## Architektura

Projekt je napsán pomocí **[STB-style libraries](https://github.com/nothings/stb?tab=readme-ov-file#how-do-i-use-these-libraries)** (single-file header knihovny). Tento přístup umožňuje snadnou přenositelnost a kompilaci jednotlivých, zaměřených částí kódu. Skládá se ze tří hlavních částí:

### 1. `http.h` (cross-platform HTTP server)
Toto je jádro, vytvořené na základě zadání, celého projektu. 
Tato knihovna abstrahuje HTTP komunikaci a parsování HTTP requestů.
Byla navržena tak, aby byla nezávislá na platformě (lze ji zkompilovat na macOS, Linuxu, Windows i další \[dos/unix\]-like).

Chování implementace lze ovlivnit pomocí definování následujících proměnných:
- `HTTP_IMPLEMENTATION` - povoluje implementaci. Defaultně není nadefinovaná.
- `HTTP_IMPLEMENTATION_LOG_IP` - http server bude při každém přijatém připojení logovat IP adresu klienta. Defaultně má hodnotu `false`.
- `HTTP_IMPLEMENTATION_MAX_REQ_HEADERS_LEN` - jedná se o maximální velikost hlaviček, kterou server příjme. Defaultně má hodnotu `8192` (8Kb), stejné jako [apache tomcat 6](https://www.geekersdigest.com/max-http-request-header-size-server-comparison/).
- `HTTP_IMPLEMENTATION_MAX_REQ_BODY_LEN` - jedná se o maximální velikost těla requetsu, kterou server příjem. Defaultně má hodnotu `8388608` (8MB).

### 2. `sys.h` (abstrakce pro MacOS System API)
Tento soubor je závislý na MacOS a poskytuje abstrakce pro čtení systémových informací, které server vrací.

Chování implementace lze ovlivnit pomocí definování následujících proměnných:
- `SYS_IMPLEMENTATION` - povoluje implementaci. Defaultně není nadefinovaná.

### 3. `server.c` (aplikační logika využívající STB libs)
Jedná se o aplikaci, která propojuje `http.h` a `sys.h`. 
- Využívá **multithreading** pro obsluhu více klientů najednou.
- Sloužít jako api, ale zároveň i zasílá soubory stránky klientovi.
- Definuje následující endpointy:
  - `GET /api/cpu` – Zátěž procesoru v procentech.
  - `GET /api/memory` – Alokace paměti v bytech.
  - `GET /api/network` – Aktuální síťové i/o v bytech za sekundu.
  - `GET /api/disk` – Volné a obsazené místo na primárním disku.
  - `GET /api/battery` – Stav baterie (procenta, nabíjení).
  - `GET /api/uptime` – Doba běhu systému v sekundách.
  - `GET /api/health` – Status serveru pro monitoring aplikace.
  - `GET /`, `GET /index`, `GET /index.html` – Hlavní stránka (`./public/index.html`).
  - `GET /favicon`, `GET /favicon.ico` – Ikona webu (`./public/favicon.ico`).
  - Ostatní neznámé cesty automaticky vracejí stránku `./public/404.html`.

------

## Kompilace

Projekt pro sestavení využívá `Makefile` a ke kompilaci `clang`. 
Jelikož kód sahá na specifická volání jádra a frameworky systému macOS (`mach`, `IOKit`, `CoreFoundation`), `Makefile` automaticky přikládá potřebné linkovací flagy. 
Při sestavení se vytvoří složka `bin`, do které se
- zkompiluje binárka serveru 
- při `prod` se vytvoří i statické knihovny z `http.h` a `sys.h`
- zkopíruje se do ní adresář `public` se statickými soubory.

### Dostupné příkazy:

```bash
# Udělá `make dev` a poté `make run`
make

# Zkompiluje dev verzi s debuug info
make dev

# Sestaví produkční (prod) verzi s maximální optimalizací (-O3)
# Zároveň separátně předkompiluje http.h a sys.h do statických knihoven
make prod

# Spustí zkompilovaný server (./bin/server)
make run

# Promaže build (smaže složku bin se vším obsahem)
make clean
```

Po spuštění server naslouchá na defaultním portu (8888). 
Monitorovací dashboard je na [http://localhost:8888/](http://localhost:8888/) v prohlížeči.

------

## Bezpečnostní analýza

Jelikož je C low-level jazyk, musel jsem si hlídat spoustu věcí ručně. Zvlášť když jde o HTTP server, do kterého zvenčí proudí nedůvěryhodná data. V projektu jsem se zaměřil na následující rizika a jejich řešení:

1. **Memory safety**
   - Těla requestů a hlavičky alokuji dynamicky a následně se po odeslání odpovědi vždy uvolňují voláním `http_request_free()`. Zároveň všude kontroluju, jestli alokace neselhala.
2. **Čtení ze socketu**
   - Request line a hlavičky čtu ze socketu záměrně jednoduše byte po bytu. Tím jsem se vyhnul složité implementaci, ale z hlediska výkonu to není ideální (co byte, to syscall). Server je taky kvůli tomu vulnerable vůči **[Slowloris útok](https://en.wikipedia.org/wiki/Slowloris_(cyber_attack))**.
3. **Zahlcení CPU**
   - V aktuální verzi používám klasický multithreading, co request, to nové vlákno přes `pthread_create`. Kdyby mi tam někdo poslal tisíce požadavků naráz, server spadne na zahlcení systémových prostředků. Ideálním řešením do budoucna by byl **[Thread Pool](https://en.wikipedia.org/wiki/Thread_pool)**, nebo **[Green Threads](https://en.wikipedia.org/wiki/Green_thread)** nebo **[jedna z implementací non-blocking I/O](https://en.wikipedia.org/wiki/Asynchronous_I/O)**.
4. **Buffer Overflow**
   - Aby mi někdo neshodil server tím, že pošle gigabytový request a sežere veškerou paměť, mám v `http.h` natvrdo nastavené limity: max 8 KB pro hlavičky a 8 MB pro tělo. Navíc při jakémkoliv skládání a práci s pamětí používám funkce s explicitně danou velikostí bufferu, abych zabránil **[buffer-overflow](https://en.wikipedia.org/wiki/Buffer_overflow)**.
5. **Path Traversal**
   - Jendá se o problém servírování souborů přes cesty typu `../../../etc/passwd`, kdy potom útočník může získat přístup k jiným, důležitý, souborům na serveru. Tomuto vektoru jsem se vyhnul úplně. Nemám tam žádnou složitou sanitizaci cest, prostě používám hardcodovaný exact-match routing. Soubor naservíruji jen a pouze tehdy, když URL přesně odpovídá stringu v kódu.
6. **Pouze HTTP**
   - Celé to běží na čistém, NEšifrovaném HTTP. Pro lokální monitoring macOS to stačí, ale kdyby to mělo jít do produkce na veřejnou IP, musel by se před to postavit třeba Nginx jako **[reverse proxy](https://en.wikipedia.org/wiki/Reverse_proxy)** s TLS certifikátem, nebo by se musela nalinkovat nějaká ssl knihovna.

------

## Použití AI

Zadáním projektu bylo napsat webový server v C. Vytváření GUI tak bylo out-of-scope tohoto projektu.

Z toho důvodu jsou soubory `public/index.html` a `public/404.html` vygenerovány pomocí LLM modelu **Gemini 3.0** (na základě mnou dodaného popisu API a vize dashboardu). 
Zbytek kódu, a všeho co je součástí projektu, jsou napsány mnou.
