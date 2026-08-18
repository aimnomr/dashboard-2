# 017 · GEN1 vs GEN2 structural comparison

**Date** 2026-08-18
**Type** investigation
**Refs** ISS-02, ISS-09

## What

Wrote `wiki/source/firmware/gen1-vs-gen2.md` to support rewriting GEN2 as flight
firmware: what the two builds share, what each has that the other lacks, and a suggested
merge order — with the simulation factored out throughout.

## Why

Aiman is rewriting GEN2 using GEN1 as reference and asked for the difference between
them. Confirmed first that **no `.ino` file exists anywhere on disk** — `ISS-09` biting
exactly where predicted. The comparison is therefore reconstructed from the wiki extracts
plus a reading of the files earlier the same day, and the document says so at the top.

Writing it down now rather than answering in conversation only: the reading that supports
it is not durable, and this is the second time the same information has had to be
recovered.

## Result

**The finding that matters: both builds want HSPI.**

| | LoRa | SD card |
|---|---|---|
| GEN1 | default SPI | **HSPI** |
| GEN2 | **HSPI** | none |

GEN1 deliberately put the SD card on HSPI to keep it clear of the LoRa bus. GEN2, having
no SD card, took HSPI for LoRa. Merged as written, both drivers initialise the same
peripheral. This is a one-line decision if made before anything else is written, and a
mystifying bus fault if found afterwards.

Also flagged for the rewrite: GEN1's GPS speed-offset calibration runs **inside the GPS
display function**, so it only happens once that OLED screen has been shown. A latent bug
worth fixing during the rewrite rather than carrying forward.

Recommended starting from GEN1 rather than GEN2 — GEN1 is real flight code, GEN2 is a
test harness with one good receive routine in it. Porting the smaller, well-understood
addition into the working article is the lower-risk direction.

**Limits stated in the document.** Structural claims are reliable; the sensor code is not
reproduced and must be copied from the real file. Register sequences and calibration
loops retyped from prose are how subtle errors get introduced.

Dashboard impact: none. GEN2's 17-field form is already canonical, implemented and
tested.
