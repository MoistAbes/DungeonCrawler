# Instrukcja Tworzenia Samodzielnej Gry (Packaging & Build Guide)

Narzędzia znajdują się w katalogu: [`Tools/Build/`](file:///e:/UE_PROJECTS/MyProject/Tools/Build/)

Katalog docelowy na dysku dla wszystkich kompilacji:
`F:\UNREAL_ENGINE_BUILDS\`

---

## 1. Jak uruchomić (Dwie metody)

### Metoda A: Jednym kliknięciem (Menu interaktywne w Windows)
1. Zamknij Unreal Editor (aby zwolnić 100% pamięci RAM na kompilację i gotowanie).
2. Przejdź w Eksploratorze Windows do:
   [`Tools/Build/package_game.bat`](file:///e:/UE_PROJECTS/MyProject/Tools/Build/package_game.bat)
3. Kliknij plik dwukrotnie.
4. Wybierz z klawiatury opcję:
   * **`[1]` Development:** Wersja testowa z włączoną konsolą (`~`) i logami.
   * **`[2]` Shipping:** Wersja zoptymalizowana, lżejsza i szybsza dla graczy.
   * **`[3]` Development + ZIP:** Tworzy folder ORAZ pojedynczy plik `.zip` z datą i godziną.
   * **`[4]` Shipping + ZIP:** Tworzy folder ORAZ pojedynczy plik `.zip` z datą i godziną.

---

### Metoda B: Z wiersza poleceń / PowerShell
Możesz uruchomić skrypt bezpośrednio z poziomu terminala:

```powershell
# Zwykła wersja testowa z datą w nazwie folderu:
powershell -ExecutionPolicy Bypass -File "Tools\Build\package_game.ps1" -Config Development

# Wersja testowa z automatycznym spakowaniem do pojedynczego pliku .ZIP:
powershell -ExecutionPolicy Bypass -File "Tools\Build\package_game.ps1" -Config Development -Zip

# Zoptymalizowana wersja dla graczy ze spakowaniem do ZIP:
powershell -ExecutionPolicy Bypass -File "Tools\Build\package_game.ps1" -Config Shipping -Zip
```

---

## 2. Gdzie trafia gotowa gra?

Każdy uruchomiony build otrzymuje unikalną nazwę z konfiguracją i znacznikiem czasu:
* **Folder z grą:**
  `F:\UNREAL_ENGINE_BUILDS\DungeonCrawler_Development_RRRR-MM-DD_GG-mm\Windows\`
  Plik startowy:
  `.../Windows/MyProject.exe`
* **Pojedynczy plik archiwalny (przy opcji `-Zip`):**
  `F:\UNREAL_ENGINE_BUILDS\DungeonCrawler_Development_RRRR-MM-DD_GG-mm.zip`
  Ten pojedynczy plik `.zip` możesz od razu przesłać komukolwiek lub wrzucić na chmurę.

---

## 3. Czas trwania i Iteracyjność
* **Pierwsze uruchomienie (Initial Cook):** Może potrwać od 15 do 35 minut, ponieważ silnik musi po raz pierwszy przygotować i zapisać wszystkie shadery Lumena, Substrate i tekstury.
* **Kolejne uruchomienia (Iterative Cook):** Dzięki włączonej fladze `-iterativecooking`, kolejne budowanie trwa zwykle zaledwie **1–3 minuty**, ponieważ silnik aktualizuje wyłącznie zmienione pliki!
