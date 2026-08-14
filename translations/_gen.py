#!/usr/bin/env python3
"""Generate translated .ts files from the en_US source template.

This is a maintenance helper: run `lupdate` first to refresh dtk-update_en_US.ts,
then run this script to (re)fill translations for the other languages while keeping
the structure and <location> tags intact.
"""
import re
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent

# Mapping of source English string -> {lang: translation}
# Only the four non-English targets need entries; en_US keeps the source.
T = {
    "zh_CN": {},
    "es": {},
    "fr": {},
    "de": {},
}

zh = T["zh_CN"]
es = T["es"]
fr = T["fr"]
de = T["de"]

# --- DtkUpdatePlugin / MainWindow / TrayPopup shared UI strings ---
zh["Backend: %1"] = "后端：%1"
zh["Dtk Update"] = "DTK 更新"
zh["Check for Updates"] = "检查更新"
zh["Update Now"] = "立即更新"
zh["Open Update Manager"] = "打开更新管理器"
zh["Settings"] = "设置"
zh["About"] = "关于"
zh["Security advisory before update"] = "更新前安全公告"
zh["The following packages have security-relevant updates (overall severity: %1). Review the details and decide whether to proceed."] = "以下软件包有安全相关更新（总体严重程度：%1）。请查看详情并决定是否继续。"
zh["A system reboot will be required after this update."] = "此次更新后需要重启系统。"
zh["Service needs restart: "] = "需要重启的服务："
zh["Config file to review: "] = "需要查看的配置文件："
zh["No changes will be made unless you choose to continue."] = "除非您选择继续，否则不会做任何更改。"
zh["Cancel"] = "取消"
zh["Update Anyway"] = "仍然更新"
zh["Update completed — attention required"] = "更新完成 — 需要注意"
zh["A system reboot is recommended (kernel or base library updated)."] = "建议重启系统（内核或基础库已更新）。"
zh["OK"] = "确定"
zh["Checking for updates…"] = "正在检查更新…"
zh["Check"] = "检查"
zh["Dependency"] = "依赖"
zh["Select a package and click 'Dependency' to analyze."] = "选择一个软件包并点击“依赖”进行分析。"
zh["Updating…"] = "正在更新…"
zh["Last check failed. Click 'Check' to retry."] = "上次检查失败。点击“检查”重试。"
zh["System up to date"] = "系统已是最新"
zh["%1 updates available"] = "有 %1 个更新可用"
zh["Analyzing dependencies for %1 …"] = "正在分析 %1 的依赖…"
zh["Failed to resolve: %1"] = "解析失败：%1"
zh["Packages to be installed:"] = "将要安装的软件包："
zh["None"] = "无"
zh["Packages to be removed:"] = "将要移除的软件包："
zh["Confirm System Update"] = "确认系统更新"
zh["The following packages will be upgraded. This action modifies the system and may affect dependencies. Continue?"] = "以下软件包将被升级。此操作会修改系统并可能影响依赖关系。是否继续？"
zh["No changes are made unless you choose to continue. Optional dependencies and orphan removal follow your settings."] = "除非您选择继续，否则不会做任何更改。可选依赖与孤立包移除均遵循您的设置。"
zh["Security advisories:"] = "安全公告："
zh["details"] = "详情"
zh["A system reboot will be required after this update (kernel or base library changed)."] = "此次更新后需要重启系统（内核或基础库已变更）。"
zh["Service needs restart"] = "需要重启的服务"
zh["Config file to review"] = "需要查看的配置文件"
zh["Pre-update checks:"] = "更新前检查："
zh["A system reboot is recommended (kernel or base library updated)."] = "建议重启系统（内核或基础库已更新）。"
zh["Updating… %1"] = "正在更新… %1"
zh["Update Completed"] = "更新已完成"
zh["Update Failed"] = "更新失败"
zh["System packages have been updated."] = "系统软件包已更新。"
zh["Update failed: %1"] = "更新失败：%1"
zh["Last check failed. Click to retry."] = "上次检查失败。点击重试。"
zh["…and %1 more"] = "…以及另外 %1 个"

# --- New health-check fields (failed units / residual packages / cache) ---
zh["Failed service unit"] = "失败的服务单元"
zh["Failed service unit: "] = "失败的服务单元："
zh["Residual / orphan package"] = "残留 / 孤立软件包"
zh["Residual / orphan package: "] = "残留 / 孤立软件包："
zh["Download cache can be cleaned: %1 MB"] = "可清理的下载缓存：%1 MB"

# --- Backend operation progress ---
zh["Installing"] = "正在安装"

# --- SecurityAdvisor ---
zh["Security-sensitive package update"] = "安全敏感软件包更新"
zh["This package is security-sensitive. Review the changelog before updating."] = "此软件包属于安全敏感类型。更新前请查看更新日志。"

# --- Generic tray shared UI strings ---
zh["Quit"] = "退出"
zh["%1 update(s) available"] = "有 %1 个更新可用"
zh["Updates available"] = "有更新可用"
zh["%1 package(s) can be updated."] = "有 %1 个软件包可更新。"

# --- Backend operation progress (PackageBackend template) ---
zh["Removing"] = "正在移除"
zh["Purging"] = "正在清除配置"
zh["Removing orphans"] = "正在移除孤立包"
zh["Cleaning cache"] = "正在清理缓存"
zh["Working"] = "正在处理"

# --- UpdateMonitor ---
zh["Another update is already in progress"] = "已有其他更新正在进行中"

# --- Linyaps availability dialog (shared) ---
zh["Linyaps Environment Issue"] = "玲珑运行环境异常"
zh["The Linyaps (玲珑) runtime environment is abnormal; sandbox application updates are unavailable. Please check the ll-cli installation and runtime."] = "玲珑（Linyaps）运行环境异常，沙箱应用更新不可用。请检查 ll-cli 的安装与运行环境。"

# --- Spanish ---
es["Backend: %1"] = "Backend: %1"
es["Dtk Update"] = "Dtk Update"
es["Check for Updates"] = "Buscar actualizaciones"
es["Update Now"] = "Actualizar ahora"
es["Open Update Manager"] = "Abrir el gestor de actualizaciones"
es["Settings"] = "Configuración"
es["About"] = "Acerca de"
es["Security advisory before update"] = "Aviso de seguridad antes de actualizar"
es["The following packages have security-relevant updates (overall severity: %1). Review the details and decide whether to proceed."] = "Los siguientes paquetes tienen actualizaciones relacionadas con la seguridad (gravedad total: %1). Revise los detalles y decida si continuar."
es["A system reboot will be required after this update."] = "Será necesario reiniciar el sistema después de esta actualización."
es["Service needs restart: "] = "Servicio que debe reiniciarse: "
es["Config file to review: "] = "Archivo de configuración que revisar: "
es["No changes will be made unless you choose to continue."] = "No se realizará ningún cambio a menos que elija continuar."
es["Cancel"] = "Cancelar"
es["Update Anyway"] = "Actualizar de todos modos"
es["Update completed — attention required"] = "Actualización completada — se requiere atención"
es["A system reboot is recommended (kernel or base library updated)."] = "Se recomienda reiniciar el sistema (se actualizó el kernel o una biblioteca base)."
es["OK"] = "Aceptar"
es["Checking for updates…"] = "Buscando actualizaciones…"
es["Check"] = "Buscar"
es["Dependency"] = "Dependencias"
es["Select a package and click 'Dependency' to analyze."] = "Seleccione un paquete y haga clic en «Dependencias» para analizarlo."
es["Updating…"] = "Actualizando…"
es["Last check failed. Click 'Check' to retry."] = "La última comprobación falló. Haga clic en «Buscar» para reintentar."
es["System up to date"] = "El sistema está actualizado"
es["%1 updates available"] = "Hay %1 actualizaciones disponibles"
es["Analyzing dependencies for %1 …"] = "Analizando dependencias de %1…"
es["Failed to resolve: %1"] = "Error al resolver: %1"
es["Packages to be installed:"] = "Paquetes que se instalarán:"
es["None"] = "Ninguno"
es["Packages to be removed:"] = "Paquetes que se eliminarán:"
es["Confirm System Update"] = "Confirmar actualización del sistema"
es["The following packages will be upgraded. This action modifies the system and may affect dependencies. Continue?"] = "Se actualizarán los siguientes paquetes. Esta acción modifica el sistema y puede afectar a las dependencias. ¿Continuar?"
es["No changes are made unless you choose to continue. Optional dependencies and orphan removal follow your settings."] = "No se realizará ningún cambio a menos que elija continuar. Las dependencias opcionales y la eliminación de huérfanos siguen su configuración."
es["Security advisories:"] = "Avisos de seguridad:"
es["details"] = "detalles"
es["A system reboot will be required after this update (kernel or base library changed)."] = "Será necesario reiniciar el sistema después de esta actualización (cambió el kernel o una biblioteca base)."
es["Service needs restart"] = "Servicio que debe reiniciarse"
es["Config file to review"] = "Archivo de configuración que revisar"
es["Pre-update checks:"] = "Comprobaciones previas a la actualización:"
es["A system reboot is recommended (kernel or base library updated)."] = "Se recomienda reiniciar el sistema (se actualizó el kernel o una biblioteca base)."
es["Updating… %1"] = "Actualizando… %1"
es["Update Completed"] = "Actualización completada"
es["Update Failed"] = "Actualización fallida"
es["System packages have been updated."] = "Los paquetes del sistema se han actualizado."
es["Update failed: %1"] = "Error al actualizar: %1"
es["Last check failed. Click to retry."] = "La última comprobación falló. Haga clic para reintentar."
es["…and %1 more"] = "…y %1 más"
es["Failed service unit"] = "Unidad de servicio fallida"
es["Failed service unit: "] = "Unidad de servicio fallida: "
es["Residual / orphan package"] = "Paquete residual / huérfano"
es["Residual / orphan package: "] = "Paquete residual / huérfano: "
es["Download cache can be cleaned: %1 MB"] = "Caché de descargas que puede limpiarse: %1 MB"
es["Installing"] = "Instalando"
es["Security-sensitive package update"] = "Actualización de paquete sensible para la seguridad"
es["This package is security-sensitive. Review the changelog before updating."] = "Este paquete es sensible para la seguridad. Revise el registro de cambios antes de actualizar."
es["Quit"] = "Salir"
es["%1 update(s) available"] = "Hay %1 actualizaciones disponibles"
es["Updates available"] = "Hay actualizaciones disponibles"
es["%1 package(s) can be updated."] = "Hay %1 paquetes que se pueden actualizar."
es["Removing"] = "Eliminando"
es["Purging"] = "Purga"
es["Removing orphans"] = "Eliminando huérfanos"
es["Cleaning cache"] = "Limpiando caché"
es["Working"] = "Trabajando"
es["Another update is already in progress"] = "Ya hay otra actualización en curso"
es["Linyaps Environment Issue"] = "Problema del entorno Linyaps"
es["The Linyaps (玲珑) runtime environment is abnormal; sandbox application updates are unavailable. Please check the ll-cli installation and runtime."] = "El entorno de ejecución de Linyaps (玲珑) es anormal; las actualizaciones de aplicaciones en sandbox no están disponibles. Verifique la instalación y el entorno de ll-cli."

# --- French ---
fr["Backend: %1"] = "Backend : %1"
fr["Dtk Update"] = "Dtk Update"
fr["Check for Updates"] = "Rechercher des mises à jour"
fr["Update Now"] = "Mettre à jour maintenant"
fr["Open Update Manager"] = "Ouvrir le gestionnaire de mises à jour"
fr["Settings"] = "Paramètres"
fr["About"] = "À propos"
fr["Security advisory before update"] = "Avis de sécurité avant la mise à jour"
fr["The following packages have security-relevant updates (overall severity: %1). Review the details and decide whether to proceed."] = "Les paquets suivants comportent des mises à jour liées à la sécurité (gravité globale : %1). Consultez les détails et décidez si vous continuez."
fr["A system reboot will be required after this update."] = "Un redémarrage du système sera nécessaire après cette mise à jour."
fr["Service needs restart: "] = "Service à redémarrer : "
fr["Config file to review: "] = "Fichier de configuration à vérifier : "
fr["No changes will be made unless you choose to continue."] = "Aucune modification ne sera effectuée à moins que vous ne choisissiez de continuer."
fr["Cancel"] = "Annuler"
fr["Update Anyway"] = "Mettre à jour quand même"
fr["Update completed — attention required"] = "Mise à jour terminée — attention requise"
fr["A system reboot is recommended (kernel or base library updated)."] = "Un redémarrage du système est recommandé (noyau ou bibliothèque de base mise à jour)."
fr["OK"] = "OK"
fr["Checking for updates…"] = "Recherche de mises à jour…"
fr["Check"] = "Vérifier"
fr["Dependency"] = "Dépendances"
fr["Select a package and click 'Dependency' to analyze."] = "Sélectionnez un paquet et cliquez sur « Dépendances » pour l'analyser."
fr["Updating…"] = "Mise à jour…"
fr["Last check failed. Click 'Check' to retry."] = "La dernière vérification a échoué. Cliquez sur « Vérifier » pour réessayer."
fr["System up to date"] = "Système à jour"
fr["%1 updates available"] = "%1 mises à jour disponibles"
fr["Analyzing dependencies for %1 …"] = "Analyse des dépendances de %1…"
fr["Failed to resolve: %1"] = "Échec de la résolution : %1"
fr["Packages to be installed:"] = "Paquets à installer :"
fr["None"] = "Aucun"
fr["Packages to be removed:"] = "Paquets à supprimer :"
fr["Confirm System Update"] = "Confirmer la mise à jour du système"
fr["The following packages will be upgraded. This action modifies the system and may affect dependencies. Continue?"] = "Les paquets suivants seront mis à niveau. Cette action modifie le système et peut affecter les dépendances. Continuer ?"
fr["No changes are made unless you choose to continue. Optional dependencies and orphan removal follow your settings."] = "Aucune modification n'est effectuée à moins que vous ne choisissiez de continuer. Les dépendances optionnelles et la suppression des orphelins suivent vos paramètres."
fr["Security advisories:"] = "Avis de sécurité :"
fr["details"] = "détails"
fr["A system reboot will be required after this update (kernel or base library changed)."] = "Un redémarrage du système sera nécessaire après cette mise à jour (noyau ou bibliothèque de base modifié)."
fr["Service needs restart"] = "Service à redémarrer"
fr["Config file to review"] = "Fichier de configuration à vérifier"
fr["Pre-update checks:"] = "Vérifications avant mise à jour :"
fr["A system reboot is recommended (kernel or base library updated)."] = "Un redémarrage du système est recommandé (noyau ou bibliothèque de base mise à jour)."
fr["Updating… %1"] = "Mise à jour… %1"
fr["Update Completed"] = "Mise à jour terminée"
fr["Update Failed"] = "Échec de la mise à jour"
fr["System packages have been updated."] = "Les paquets système ont été mis à jour."
fr["Update failed: %1"] = "Échec de la mise à jour : %1"
fr["Last check failed. Click to retry."] = "La dernière vérification a échoué. Cliquez pour réessayer."
fr["…and %1 more"] = "…et %1 de plus"
fr["Failed service unit"] = "Unité de service en échec"
fr["Failed service unit: "] = "Unité de service en échec : "
fr["Residual / orphan package"] = "Paquet résiduel / orphelin"
fr["Residual / orphan package: "] = "Paquet résiduel / orphelin : "
fr["Download cache can be cleaned: %1 MB"] = "Cache de téléchargement pouvant être nettoyé : %1 Mo"
fr["Installing"] = "Installation"
fr["Security-sensitive package update"] = "Mise à jour de paquet sensible à la sécurité"
fr["This package is security-sensitive. Review the changelog before updating."] = "Ce paquet est sensible à la sécurité. Consultez le journal des modifications avant la mise à jour."
fr["Quit"] = "Quitter"
fr["%1 update(s) available"] = "%1 mises à jour disponibles"
fr["Updates available"] = "Mises à jour disponibles"
fr["%1 package(s) can be updated."] = "%1 paquet(s) peuvent être mis à jour."
fr["Removing"] = "Suppression"
fr["Purging"] = "Purge"
fr["Removing orphans"] = "Suppression des orphelins"
fr["Cleaning cache"] = "Nettoyage du cache"
fr["Working"] = "Traitement"
fr["Another update is already in progress"] = "Une autre mise à jour est déjà en cours"
fr["Linyaps Environment Issue"] = "Problème d'environnement Linyaps"
fr["The Linyaps (玲珑) runtime environment is abnormal; sandbox application updates are unavailable. Please check the ll-cli installation and runtime."] = "L'environnement d'exécution Linyaps (玲珑) est anormal ; les mises à jour d'applications en sandbox sont indisponibles. Vérifiez l'installation et l'environnement de ll-cli."

# --- German ---
de["Backend: %1"] = "Backend: %1"
de["Dtk Update"] = "Dtk Update"
de["Check for Updates"] = "Nach Aktualisierungen suchen"
de["Update Now"] = "Jetzt aktualisieren"
de["Open Update Manager"] = "Update-Manager öffnen"
de["Settings"] = "Einstellungen"
de["About"] = "Über"
de["Security advisory before update"] = "Sicherheitshinweis vor der Aktualisierung"
de["The following packages have security-relevant updates (overall severity: %1). Review the details and decide whether to proceed."] = "Folgende Pakete enthalten sicherheitsrelevante Aktualisierungen (Gesamtschweregrad: %1). Prüfen Sie die Details und entscheiden Sie, ob fortgefahren wird."
de["A system reboot will be required after this update."] = "Nach dieser Aktualisierung ist ein Neustart des Systems erforderlich."
de["Service needs restart: "] = "Neu zu startender Dienst: "
de["Config file to review: "] = "Zu prüfende Konfigurationsdatei: "
de["No changes will be made unless you choose to continue."] = "Es werden keine Änderungen vorgenommen, sofern Sie nicht fortfahren."
de["Cancel"] = "Abbrechen"
de["Update Anyway"] = "Trotzdem aktualisieren"
de["Update completed — attention required"] = "Aktualisierung abgeschlossen — Beachtung erforderlich"
de["A system reboot is recommended (kernel or base library updated)."] = "Ein Neustart des Systems wird empfohlen (Kernel oder Basisbibliothek aktualisiert)."
de["OK"] = "OK"
de["Checking for updates…"] = "Suche nach Aktualisierungen…"
de["Check"] = "Prüfen"
de["Dependency"] = "Abhängigkeiten"
de["Select a package and click 'Dependency' to analyze."] = "Wählen Sie ein Paket und klicken Sie auf „Abhängigkeiten“, um es zu analysieren."
de["Updating…"] = "Aktualisierung…"
de["Last check failed. Click 'Check' to retry."] = "Die letzte Prüfung ist fehlgeschlagen. Klicken Sie auf „Prüfen“, um es erneut zu versuchen."
de["System up to date"] = "System ist aktuell"
de["%1 updates available"] = "%1 Aktualisierungen verfügbar"
de["Analyzing dependencies for %1 …"] = "Analysiere Abhängigkeiten von %1…"
de["Failed to resolve: %1"] = "Auflösung fehlgeschlagen: %1"
de["Packages to be installed:"] = "Zu installierende Pakete:"
de["None"] = "Keine"
de["Packages to be removed:"] = "Zu entfernende Pakete:"
de["Confirm System Update"] = "Systemaktualisierung bestätigen"
de["The following packages will be upgraded. This action modifies the system and may affect dependencies. Continue?"] = "Folgende Pakete werden aktualisiert. Dieser Vorgang verändert das System und kann Abhängigkeiten beeinflussen. Fortfahren?"
de["No changes are made unless you choose to continue. Optional dependencies and orphan removal follow your settings."] = "Es werden keine Änderungen vorgenommen, sofern Sie nicht fortfahren. Optionale Abhängigkeiten und die Entfernung verwaister Pakete folgen Ihren Einstellungen."
de["Security advisories:"] = "Sicherheitshinweise:"
de["details"] = "Details"
de["A system reboot will be required after this update (kernel or base library changed)."] = "Nach dieser Aktualisierung ist ein Neustart des Systems erforderlich (Kernel oder Basisbibliothek geändert)."
de["Service needs restart"] = "Neu zu startender Dienst"
de["Config file to review"] = "Zu prüfende Konfigurationsdatei"
de["Pre-update checks:"] = "Prüfungen vor der Aktualisierung:"
de["A system reboot is recommended (kernel or base library updated)."] = "Ein Neustart des Systems wird empfohlen (Kernel oder Basisbibliothek aktualisiert)."
de["Updating… %1"] = "Aktualisierung… %1"
de["Update Completed"] = "Aktualisierung abgeschlossen"
de["Update Failed"] = "Aktualisierung fehlgeschlagen"
de["System packages have been updated."] = "Die Systempakete wurden aktualisiert."
de["Update failed: %1"] = "Aktualisierung fehlgeschlagen: %1"
de["Last check failed. Click to retry."] = "Die letzte Prüfung ist fehlgeschlagen. Klicken zum erneuten Versuch."
de["…and %1 more"] = "…und %1 weitere"
de["Failed service unit"] = "Fehlgeschlagene Diensteinheit"
de["Failed service unit: "] = "Fehlgeschlagene Diensteinheit: "
de["Residual / orphan package"] = "Rest-/Waisenpaket"
de["Residual / orphan package: "] = "Rest-/Waisenpaket: "
de["Download cache can be cleaned: %1 MB"] = "Bereinigbarer Download-Cache: %1 MB"
de["Installing"] = "Installieren"
de["Security-sensitive package update"] = "Sicherheitsrelevantes Paket-Update"
de["This package is security-sensitive. Review the changelog before updating."] = "Dieses Paket ist sicherheitsrelevant. Prüfen Sie das Änderungsprotokoll vor der Aktualisierung."
de["Quit"] = "Beenden"
de["%1 update(s) available"] = "%1 Aktualisierungen verfügbar"
de["Updates available"] = "Aktualisierungen verfügbar"
de["%1 package(s) can be updated."] = "%1 Paket(e) können aktualisiert werden."
de["Removing"] = "Entfernen"
de["Purging"] = "Bereinigen"
de["Removing orphans"] = "Verwaiste entfernen"
de["Cleaning cache"] = "Cache bereinigen"
de["Working"] = "Wird verarbeitet"
de["Another update is already in progress"] = "Eine andere Aktualisierung läuft bereits"
de["Linyaps Environment Issue"] = "Linyaps-Umgebungsproblem"
de["The Linyaps (玲珑) runtime environment is abnormal; sandbox application updates are unavailable. Please check the ll-cli installation and runtime."] = "Die Linyaps-Laufzeitumgebung (玲珑) ist fehlerhaft; Sandbox-Anwendungsupdates sind nicht verfügbar. Bitte prüfen Sie die ll-cli-Installation und -Laufzeitumgebung."


def fill_ts(lang: str, table: dict):
    src = BASE / "dtk-update_en_US.ts"
    out = BASE / f"dtk-update_{lang}.ts"
    text = src.read_text(encoding="utf-8")
    # Replace header language
    text = text.replace('language="en_US"', f'language="{lang}"')
    # For each source string present in the table, fill its translation.
    # The .ts stores apostrophes as &apos;, so also try the encoded form.
    missing = []
    for src_str, tgt in table.items():
        candidates = [src_str, src_str.replace("'", "&apos;")]
        found = False
        for c in candidates:
            esc_src = re.escape(c)
            pat = re.compile(
                r"(<source>" + esc_src + r"</source>\s*<translation[^>]*>)(.*?)(</translation>)",
                re.DOTALL,
            )
            new_text, n = pat.subn(
                lambda m, t=tgt: f'{m.group(1).replace(" type=\"unfinished\"", "")}{escape(t)}{m.group(3)}', text
            )
            if n:
                found = True
                text = new_text
        if not found:
            missing.append(src_str)
    out.write_text(text, encoding="utf-8")
    if missing:
        print(f"[{lang}] WARNING: {len(missing)} strings not found:")
        for m in missing:
            print(f"    - {m!r}")
    else:
        print(f"[{lang}] OK: all {len(table)} strings translated")


def escape(s: str) -> str:
    return (s.replace("&", "&amp;")
             .replace("<", "&lt;")
             .replace(">", "&gt;"))


for lang, table in T.items():
    fill_ts(lang, table)
