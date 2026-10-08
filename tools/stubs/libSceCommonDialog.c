/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Link-time facade of the PS5 libSceCommonDialog module, which the Payload SDK has no import
 * library for: the one function the PS5 keyboard needs called before it opens
 * (headless/system_keyboard.cpp). The native packaging tool turns the symbol into an import of
 * the firmware module; nothing here is linked into the application or ever runs.
 */
int sceCommonDialogInitialize(void) { return -1; }
