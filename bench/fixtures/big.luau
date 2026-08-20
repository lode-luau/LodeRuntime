--!strict
-- Deterministic generated workload for CLI warm benchmarks (see bench/README.md).
-- Regenerate with: build/bin/Release/lode.exe bench/fixtures/gen_big.luau

local function mix(a: number, b: number): number
    local h = (a * 374761393 + b * 668265263) % 2147483647
    h = bit32.bxor(h, bit32.rshift(h, 13)) * 1274126177
    h = bit32.bxor(h, bit32.rshift(h, 16))
    return h % 2147483647
end

local function mix2(a: number, b: number, c: number): number
    return mix(mix(a, b), c)
end

local function block0(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 231794736, 231845056)
    acc = mix2(acc, 560545280, 1051956736)
    acc = mix2(acc, 774573568, 446244352)
    acc = acc % 2147483647
    acc = mix2(acc, 550067776, 905236736)
    acc = mix2(acc, 921382144, 400054528)
    acc = mix2(acc, 675026176, 771432704)
    acc = acc % 2147483647
    acc = mix2(acc, 503004416, 1049569600)
    acc = mix2(acc, 688275456, 522809344)
    acc = mix2(acc, 80011264, 68390912)
    acc = acc % 2147483647
    acc = mix2(acc, 305430592, 820923264)
    acc = mix2(acc, 360142208, 551327744)
    acc = mix2(acc, 71100416, 211744832)
    acc = acc % 2147483647
    acc = mix2(acc, 282288896, 606820096)
    acc = mix2(acc, 620116736, 617697024)
    acc = mix2(acc, 221303552, 806618880)
    acc = acc % 2147483647
    acc = mix2(acc, 1017719552, 555843328)
    acc = mix2(acc, 167068416, 165132096)
    acc = mix2(acc, 640595456, 22523392)
    acc = acc % 2147483647
    return acc
end

local function block1(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 492389888, 374799936)
    acc = mix2(acc, 704770304, 541183232)
    acc = mix2(acc, 377125120, 956544320)
    acc = acc % 2147483647
    acc = mix2(acc, 390978560, 221174848)
    acc = mix2(acc, 1032812288, 911727360)
    acc = mix2(acc, 170028800, 679179072)
    acc = acc % 2147483647
    acc = mix2(acc, 1026408960, 160942592)
    acc = mix2(acc, 288174656, 856561024)
    acc = mix2(acc, 680286080, 862613888)
    acc = acc % 2147483647
    acc = mix2(acc, 468157312, 547630528)
    acc = mix2(acc, 240659200, 983859008)
    acc = mix2(acc, 38788608, 94746112)
    acc = acc % 2147483647
    acc = mix2(acc, 688041472, 455624192)
    acc = mix2(acc, 964296256, 166260992)
    acc = mix2(acc, 396152064, 574191872)
    acc = acc % 2147483647
    acc = mix2(acc, 727102720, 487670016)
    acc = mix2(acc, 673635584, 501369088)
    acc = mix2(acc, 563561728, 40800512)
    acc = acc % 2147483647
    return acc
end

local function block2(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 524414264, 380190976)
    acc = mix2(acc, 1038015744, 758794496)
    acc = mix2(acc, 974079232, 1032229120)
    acc = acc % 2147483647
    acc = mix2(acc, 410643456, 433171520)
    acc = mix2(acc, 440534912, 426140160)
    acc = mix2(acc, 242477568, 883348032)
    acc = acc % 2147483647
    acc = mix2(acc, 479671552, 1015529728)
    acc = mix2(acc, 681270528, 243058944)
    acc = mix2(acc, 643810624, 341879808)
    acc = acc % 2147483647
    acc = mix2(acc, 697396224, 164834304)
    acc = mix2(acc, 240249856, 382315520)
    acc = mix2(acc, 776367104, 820259840)
    acc = acc % 2147483647
    acc = mix2(acc, 512683008, 381856832)
    acc = mix2(acc, 987694976, 306143744)
    acc = mix2(acc, 551086656, 20671744)
    acc = acc % 2147483647
    acc = mix2(acc, 797153536, 698766592)
    acc = mix2(acc, 666394880, 487554304)
    acc = mix2(acc, 95316288, 919896064)
    acc = acc % 2147483647
    return acc
end

local function block3(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 393916416, 14491648)
    acc = mix2(acc, 1038536704, 245809152)
    acc = mix2(acc, 192933952, 841808768)
    acc = acc % 2147483647
    acc = mix2(acc, 382223872, 479854080)
    acc = mix2(acc, 217404992, 977103232)
    acc = mix2(acc, 457907072, 106685888)
    acc = acc % 2147483647
    acc = mix2(acc, 664896256, 665777920)
    acc = mix2(acc, 1052859136, 68022016)
    acc = mix2(acc, 314635072, 700793344)
    acc = acc % 2147483647
    acc = mix2(acc, 790186496, 1045475840)
    acc = mix2(acc, 177732096, 752984640)
    acc = mix2(acc, 419546368, 231149888)
    acc = acc % 2147483647
    acc = mix2(acc, 478414976, 835636928)
    acc = mix2(acc, 546093056, 200968192)
    acc = mix2(acc, 521934912, 265945984)
    acc = acc % 2147483647
    acc = mix2(acc, 568634880, 794969600)
    acc = mix2(acc, 36437504, 136271360)
    acc = mix2(acc, 93048384, 987660672)
    acc = acc % 2147483647
    return acc
end

local function block4(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 87184256, 551468480)
    acc = mix2(acc, 522681088, 759795456)
    acc = mix2(acc, 199496448, 1072056064)
    acc = acc % 2147483647
    acc = mix2(acc, 145648640, 522988608)
    acc = mix2(acc, 885688064, 425569024)
    acc = mix2(acc, 419168000, 620610304)
    acc = acc % 2147483647
    acc = mix2(acc, 594949888, 672635648)
    acc = mix2(acc, 625422080, 698430208)
    acc = mix2(acc, 153937664, 8424256)
    acc = acc % 2147483647
    acc = mix2(acc, 346618368, 605987392)
    acc = mix2(acc, 872072448, 667685120)
    acc = mix2(acc, 85028096, 94476544)
    acc = acc % 2147483647
    acc = mix2(acc, 946623808, 932838400)
    acc = mix2(acc, 1062763520, 376765440)
    acc = mix2(acc, 479689728, 1011350528)
    acc = acc % 2147483647
    acc = mix2(acc, 24017920, 768801792)
    acc = mix2(acc, 865958912, 500562944)
    acc = mix2(acc, 249855040, 245667584)
    acc = acc % 2147483647
    return acc
end

local function block5(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 739670848, 849357312)
    acc = mix2(acc, 385749504, 531225088)
    acc = mix2(acc, 398328320, 209914432)
    acc = acc % 2147483647
    acc = mix2(acc, 778767744, 313651200)
    acc = mix2(acc, 216227904, 899918592)
    acc = mix2(acc, 919207680, 431211264)
    acc = acc % 2147483647
    acc = mix2(acc, 150222592, 45980416)
    acc = mix2(acc, 301281080, 387472128)
    acc = mix2(acc, 588836608, 611316480)
    acc = acc % 2147483647
    acc = mix2(acc, 297051904, 1053641536)
    acc = mix2(acc, 1037482496, 488202752)
    acc = mix2(acc, 732621376, 413494528)
    acc = acc % 2147483647
    acc = mix2(acc, 866490624, 136336640)
    acc = mix2(acc, 229269824, 850204800)
    acc = mix2(acc, 69901952, 426814464)
    acc = acc % 2147483647
    acc = mix2(acc, 828468288, 787924736)
    acc = mix2(acc, 431287040, 329400128)
    acc = mix2(acc, 920296960, 725186048)
    acc = acc % 2147483647
    return acc
end

local function block6(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 762015232, 641861120)
    acc = mix2(acc, 200326656, 593827392)
    acc = mix2(acc, 340928768, 628729152)
    acc = acc % 2147483647
    acc = mix2(acc, 346360832, 468601856)
    acc = mix2(acc, 127831104, 113075968)
    acc = mix2(acc, 1040649024, 32340480)
    acc = acc % 2147483647
    acc = mix2(acc, 155787832, 736154144)
    acc = mix2(acc, 327116800, 250574912)
    acc = mix2(acc, 337240960, 660016576)
    acc = acc % 2147483647
    acc = mix2(acc, 683772672, 348282624)
    acc = mix2(acc, 162833216, 791097984)
    acc = mix2(acc, 336834688, 254367424)
    acc = acc % 2147483647
    acc = mix2(acc, 629710848, 468471808)
    acc = mix2(acc, 66994240, 475335424)
    acc = mix2(acc, 999284480, 741614336)
    acc = acc % 2147483647
    acc = mix2(acc, 115152640, 321190656)
    acc = mix2(acc, 221843200, 501060352)
    acc = mix2(acc, 740411136, 1057221376)
    acc = acc % 2147483647
    return acc
end

local function block7(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 138756096, 519860288)
    acc = mix2(acc, 758086528, 970507648)
    acc = mix2(acc, 779700096, 174891520)
    acc = acc % 2147483647
    acc = mix2(acc, 362621440, 313987648)
    acc = mix2(acc, 486700416, 987314112)
    acc = mix2(acc, 469331200, 912890112)
    acc = acc % 2147483647
    acc = mix2(acc, 764695808, 403595520)
    acc = mix2(acc, 632126784, 109577216)
    acc = mix2(acc, 287433792, 679075584)
    acc = acc % 2147483647
    acc = mix2(acc, 399909632, 920065792)
    acc = mix2(acc, 748092160, 516099840)
    acc = mix2(acc, 564106048, 323969536)
    acc = acc % 2147483647
    acc = mix2(acc, 883485248, 281767168)
    acc = mix2(acc, 802396416, 733369600)
    acc = mix2(acc, 680026368, 362773760)
    acc = acc % 2147483647
    acc = mix2(acc, 989751616, 165969920)
    acc = mix2(acc, 357216320, 604027648)
    acc = mix2(acc, 793366272, 964645632)
    acc = acc % 2147483647
    return acc
end

local function block8(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 947947264, 568044288)
    acc = mix2(acc, 446824192, 749454080)
    acc = mix2(acc, 747386624, 980785920)
    acc = acc % 2147483647
    acc = mix2(acc, 458043136, 1043280704)
    acc = mix2(acc, 1060536832, 298945024)
    acc = mix2(acc, 709095936, 514472448)
    acc = acc % 2147483647
    acc = mix2(acc, 136766016, 251528448)
    acc = mix2(acc, 632180992, 233221376)
    acc = mix2(acc, 32364864, 301412472)
    acc = acc % 2147483647
    acc = mix2(acc, 735339328, 590082560)
    acc = mix2(acc, 158480896, 793698880)
    acc = mix2(acc, 721814784, 85348608)
    acc = acc % 2147483647
    acc = mix2(acc, 489598208, 865458432)
    acc = mix2(acc, 887854336, 229941504)
    acc = mix2(acc, 620150016, 413444352)
    acc = acc % 2147483647
    acc = mix2(acc, 530195776, 997952512)
    acc = mix2(acc, 4377600, 1050217472)
    acc = mix2(acc, 282217472, 78810112)
    acc = acc % 2147483647
    return acc
end

local function block9(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 783141888, 472386560)
    acc = mix2(acc, 951405568, 710511616)
    acc = mix2(acc, 415812608, 61279296)
    acc = acc % 2147483647
    acc = mix2(acc, 760997752, 259542528)
    acc = mix2(acc, 301819392, 33070592)
    acc = mix2(acc, 184514104, 739939840)
    acc = acc % 2147483647
    acc = mix2(acc, 621153792, 802694656)
    acc = mix2(acc, 513564160, 557331968)
    acc = mix2(acc, 157494784, 188663360)
    acc = acc % 2147483647
    acc = mix2(acc, 1067587840, 295884032)
    acc = mix2(acc, 490397952, 681573696)
    acc = mix2(acc, 717784064, 664298496)
    acc = acc % 2147483647
    acc = mix2(acc, 576373760, 543007744)
    acc = mix2(acc, 668402688, 298116096)
    acc = mix2(acc, 642597888, 832447488)
    acc = acc % 2147483647
    acc = mix2(acc, 659391488, 842621952)
    acc = mix2(acc, 921158656, 375825408)
    acc = mix2(acc, 665584704, 386498304)
    acc = acc % 2147483647
    return acc
end

local function block10(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 767378176, 905950976)
    acc = mix2(acc, 832772864, 917286656)
    acc = mix2(acc, 1054388992, 986605312)
    acc = acc % 2147483647
    acc = mix2(acc, 390335232, 626188096)
    acc = mix2(acc, 265207296, 753604160)
    acc = mix2(acc, 912271616, 695182592)
    acc = acc % 2147483647
    acc = mix2(acc, 114258176, 12668160)
    acc = mix2(acc, 96729344, 5190976)
    acc = mix2(acc, 863198208, 908483584)
    acc = acc % 2147483647
    acc = mix2(acc, 643652608, 801659904)
    acc = mix2(acc, 207051776, 121265152)
    acc = mix2(acc, 65100800, 956062720)
    acc = acc % 2147483647
    acc = mix2(acc, 259742720, 361651200)
    acc = mix2(acc, 900029504, 319026944)
    acc = mix2(acc, 401758976, 736816960)
    acc = acc % 2147483647
    acc = mix2(acc, 325946880, 988390912)
    acc = mix2(acc, 705682944, 845526528)
    acc = mix2(acc, 918728192, 904414720)
    acc = acc % 2147483647
    return acc
end

local function block11(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 490339840, 286611008)
    acc = mix2(acc, 1004366208, 208345984)
    acc = mix2(acc, 821814720, 230785792)
    acc = acc % 2147483647
    acc = mix2(acc, 592377664, 326432256)
    acc = mix2(acc, 801173056, 591208704)
    acc = mix2(acc, 655703296, 178333952)
    acc = acc % 2147483647
    acc = mix2(acc, 387753216, 796422464)
    acc = mix2(acc, 4843520, 338163712)
    acc = mix2(acc, 311740416, 776558592)
    acc = acc % 2147483647
    acc = mix2(acc, 504014848, 968472640)
    acc = mix2(acc, 733422336, 1000609536)
    acc = mix2(acc, 769653504, 685248256)
    acc = acc % 2147483647
    acc = mix2(acc, 311464704, 643352320)
    acc = mix2(acc, 175041280, 1054097216)
    acc = mix2(acc, 388275712, 918255104)
    acc = acc % 2147483647
    acc = mix2(acc, 801467904, 311690752)
    acc = mix2(acc, 651869696, 621221376)
    acc = mix2(acc, 817401344, 232302080)
    acc = acc % 2147483647
    return acc
end

local function block12(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 198368768, 1013757504)
    acc = mix2(acc, 256985344, 1071269120)
    acc = mix2(acc, 250974208, 275677184)
    acc = acc % 2147483647
    acc = mix2(acc, 705474560, 621223936)
    acc = mix2(acc, 801689600, 669040640)
    acc = mix2(acc, 892465152, 1029922816)
    acc = acc % 2147483647
    acc = mix2(acc, 714084352, 607584256)
    acc = mix2(acc, 761999360, 524525568)
    acc = mix2(acc, 713773056, 800145408)
    acc = acc % 2147483647
    acc = mix2(acc, 267939840, 518512704)
    acc = mix2(acc, 9317248, 852080056)
    acc = mix2(acc, 428908416, 271391232)
    acc = acc % 2147483647
    acc = mix2(acc, 1025524288, 409724160)
    acc = mix2(acc, 599175424, 79138048)
    acc = mix2(acc, 1025325312, 47150336)
    acc = acc % 2147483647
    acc = mix2(acc, 637206840, 261014784)
    acc = mix2(acc, 1037046080, 240715776)
    acc = mix2(acc, 744003648, 563905280)
    acc = acc % 2147483647
    return acc
end

local function block13(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 294516480, 78928640)
    acc = mix2(acc, 377810752, 587173376)
    acc = mix2(acc, 403940864, 552721984)
    acc = acc % 2147483647
    acc = mix2(acc, 506679552, 612871424)
    acc = mix2(acc, 846911744, 40670464)
    acc = mix2(acc, 463577400, 742450432)
    acc = acc % 2147483647
    acc = mix2(acc, 996651264, 152341760)
    acc = mix2(acc, 783694080, 1056208128)
    acc = mix2(acc, 129761536, 425368832)
    acc = acc % 2147483647
    acc = mix2(acc, 359336256, 585408512)
    acc = mix2(acc, 68702208, 112869440)
    acc = mix2(acc, 241240960, 366155200)
    acc = acc % 2147483647
    acc = mix2(acc, 771814144, 309438208)
    acc = mix2(acc, 625363712, 197664512)
    acc = mix2(acc, 933716800, 906162688)
    acc = acc % 2147483647
    acc = mix2(acc, 285030912, 1065214464)
    acc = mix2(acc, 796224000, 927617536)
    acc = mix2(acc, 463542784, 897860160)
    acc = acc % 2147483647
    return acc
end

local function block14(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 318760192, 750432512)
    acc = mix2(acc, 325815552, 398539072)
    acc = mix2(acc, 70717568, 360265408)
    acc = acc % 2147483647
    acc = mix2(acc, 734724096, 1064251392)
    acc = mix2(acc, 49782784, 909070392)
    acc = mix2(acc, 1072064512, 415922176)
    acc = acc % 2147483647
    acc = mix2(acc, 247810112, 126055296)
    acc = mix2(acc, 355032512, 531471104)
    acc = mix2(acc, 69545728, 841429824)
    acc = acc % 2147483647
    acc = mix2(acc, 802062848, 311003648)
    acc = mix2(acc, 144437824, 196466048)
    acc = mix2(acc, 75044864, 484403200)
    acc = acc % 2147483647
    acc = mix2(acc, 214923328, 746185472)
    acc = mix2(acc, 621795072, 624999168)
    acc = mix2(acc, 931778304, 1019171584)
    acc = acc % 2147483647
    acc = mix2(acc, 448825344, 1030363200)
    acc = mix2(acc, 990905088, 414938880)
    acc = mix2(acc, 806607680, 415369728)
    acc = acc % 2147483647
    return acc
end

local function block15(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 631927296, 609139200)
    acc = mix2(acc, 238117376, 585296384)
    acc = mix2(acc, 971624960, 311073280)
    acc = acc % 2147483647
    acc = mix2(acc, 1005568576, 264594688)
    acc = mix2(acc, 970076480, 16820224)
    acc = mix2(acc, 276281344, 218728448)
    acc = acc % 2147483647
    acc = mix2(acc, 612309056, 83952384)
    acc = mix2(acc, 39352064, 1038932792)
    acc = mix2(acc, 234132992, 987237952)
    acc = acc % 2147483647
    acc = mix2(acc, 668320000, 483482880)
    acc = mix2(acc, 1031457088, 1041850368)
    acc = mix2(acc, 739119104, 719126528)
    acc = acc % 2147483647
    acc = mix2(acc, 370749440, 895066176)
    acc = mix2(acc, 930933504, 835337984)
    acc = mix2(acc, 206504704, 579761920)
    acc = acc % 2147483647
    acc = mix2(acc, 686480128, 481914624)
    acc = mix2(acc, 348549952, 857522816)
    acc = mix2(acc, 897712256, 125920768)
    acc = acc % 2147483647
    return acc
end

local function block16(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 53255744, 397873528)
    acc = mix2(acc, 454373440, 238631680)
    acc = mix2(acc, 542658304, 128701184)
    acc = acc % 2147483647
    acc = mix2(acc, 275973952, 682608128)
    acc = mix2(acc, 86736384, 669570624)
    acc = mix2(acc, 619374848, 16999680)
    acc = acc % 2147483647
    acc = mix2(acc, 356004152, 826029312)
    acc = mix2(acc, 214483200, 575167808)
    acc = mix2(acc, 79258624, 392676352)
    acc = acc % 2147483647
    acc = mix2(acc, 753312832, 767532800)
    acc = mix2(acc, 386458368, 368236288)
    acc = mix2(acc, 535280384, 312790784)
    acc = acc % 2147483647
    acc = mix2(acc, 20861760, 677847552)
    acc = mix2(acc, 742763008, 259363328)
    acc = mix2(acc, 327899712, 357899520)
    acc = acc % 2147483647
    acc = mix2(acc, 840141120, 765665280)
    acc = mix2(acc, 573886464, 131907584)
    acc = mix2(acc, 461238336, 626195328)
    acc = acc % 2147483647
    return acc
end

local function block17(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 838223232, 269010816)
    acc = mix2(acc, 226894272, 988090112)
    acc = mix2(acc, 941200128, 531482368)
    acc = acc % 2147483647
    acc = mix2(acc, 429910784, 615595776)
    acc = mix2(acc, 447349504, 961378048)
    acc = mix2(acc, 171825920, 386953024)
    acc = acc % 2147483647
    acc = mix2(acc, 526716544, 527588480)
    acc = mix2(acc, 115380736, 102390336)
    acc = mix2(acc, 775429376, 884640000)
    acc = acc % 2147483647
    acc = mix2(acc, 1059751168, 933285888)
    acc = mix2(acc, 249085952, 482996224)
    acc = mix2(acc, 689659968, 159099648)
    acc = acc % 2147483647
    acc = mix2(acc, 969015040, 759939840)
    acc = mix2(acc, 816592640, 1008048896)
    acc = mix2(acc, 208482048, 684667648)
    acc = acc % 2147483647
    acc = mix2(acc, 868411136, 375294720)
    acc = mix2(acc, 379281152, 823763776)
    acc = mix2(acc, 992489984, 458848768)
    acc = acc % 2147483647
    return acc
end

local function block18(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 71639552, 768322112)
    acc = mix2(acc, 991374592, 110385408)
    acc = mix2(acc, 373822720, 823599424)
    acc = acc % 2147483647
    acc = mix2(acc, 712693760, 337520640)
    acc = mix2(acc, 822556672, 589498368)
    acc = mix2(acc, 307927040, 987949056)
    acc = acc % 2147483647
    acc = mix2(acc, 625801216, 626378752)
    acc = mix2(acc, 947279872, 691251200)
    acc = mix2(acc, 942585856, 938772480)
    acc = acc % 2147483647
    acc = mix2(acc, 76421120, 198207488)
    acc = mix2(acc, 929855552, 659743488)
    acc = mix2(acc, 292869888, 306297664)
    acc = acc % 2147483647
    acc = mix2(acc, 545942016, 54219264)
    acc = mix2(acc, 524163640, 952003072)
    acc = mix2(acc, 694112768, 345119232)
    acc = acc % 2147483647
    acc = mix2(acc, 572604928, 910510592)
    acc = mix2(acc, 873247232, 866384384)
    acc = mix2(acc, 251503104, 895101504)
    acc = acc % 2147483647
    return acc
end

local function block19(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 499363072, 419879168)
    acc = mix2(acc, 336106752, 588095744)
    acc = mix2(acc, 433831168, 606447936)
    acc = acc % 2147483647
    acc = mix2(acc, 85639168, 316928064)
    acc = mix2(acc, 264305408, 598019840)
    acc = mix2(acc, 299471616, 376293120)
    acc = acc % 2147483647
    acc = mix2(acc, 694151936, 426852096)
    acc = mix2(acc, 275383104, 228659840)
    acc = mix2(acc, 808980672, 293196288)
    acc = acc % 2147483647
    acc = mix2(acc, 987404800, 100647424)
    acc = mix2(acc, 117557760, 55521792)
    acc = mix2(acc, 905214520, 900024832)
    acc = acc % 2147483647
    acc = mix2(acc, 804041216, 839121408)
    acc = mix2(acc, 501064192, 179972672)
    acc = mix2(acc, 933473664, 9310080)
    acc = acc % 2147483647
    acc = mix2(acc, 37079552, 275984896)
    acc = mix2(acc, 105413120, 503849536)
    acc = mix2(acc, 23479680, 45664184)
    acc = acc % 2147483647
    return acc
end

local function block20(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 634866064, 712270464)
    acc = mix2(acc, 949035136, 816973440)
    acc = mix2(acc, 13104128, 964367360)
    acc = acc % 2147483647
    acc = mix2(acc, 830452736, 876073984)
    acc = mix2(acc, 912094208, 388142080)
    acc = mix2(acc, 449918016, 310308608)
    acc = acc % 2147483647
    acc = mix2(acc, 652079936, 270418432)
    acc = mix2(acc, 553536000, 47297024)
    acc = mix2(acc, 59046456, 53201408)
    acc = acc % 2147483647
    acc = mix2(acc, 758198840, 177185280)
    acc = mix2(acc, 243542592, 547728768)
    acc = mix2(acc, 308809728, 724500480)
    acc = acc % 2147483647
    acc = mix2(acc, 889401344, 721133568)
    acc = mix2(acc, 937644032, 774455296)
    acc = mix2(acc, 957378560, 773840896)
    acc = acc % 2147483647
    acc = mix2(acc, 433229824, 109543488)
    acc = mix2(acc, 38095744, 964842936)
    acc = mix2(acc, 921485056, 1056932608)
    acc = acc % 2147483647
    return acc
end

local function block21(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 1052047360, 647874560)
    acc = mix2(acc, 874403840, 1069262848)
    acc = mix2(acc, 1488896, 96241721)
    acc = acc % 2147483647
    acc = mix2(acc, 937266688, 513385984)
    acc = mix2(acc, 1006624256, 899003904)
    acc = mix2(acc, 842182144, 613959168)
    acc = acc % 2147483647
    acc = mix2(acc, 506068480, 390420032)
    acc = mix2(acc, 1045260672, 907746304)
    acc = mix2(acc, 873667584, 648748032)
    acc = acc % 2147483647
    acc = mix2(acc, 23272448, 834345984)
    acc = mix2(acc, 389225472, 457907200)
    acc = mix2(acc, 696458240, 123411456)
    acc = acc % 2147483647
    acc = mix2(acc, 206773312, 729724800)
    acc = mix2(acc, 232608128, 521200576)
    acc = mix2(acc, 102082816, 649536832)
    acc = acc % 2147483647
    acc = mix2(acc, 416582656, 489147456)
    acc = mix2(acc, 61712256, 117015040)
    acc = mix2(acc, 165186048, 658436608)
    acc = acc % 2147483647
    return acc
end

local function block22(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 260651520, 152691264)
    acc = mix2(acc, 7668992, 928600320)
    acc = mix2(acc, 766881024, 521197824)
    acc = acc % 2147483647
    acc = mix2(acc, 843748672, 314260480)
    acc = mix2(acc, 771800064, 932723712)
    acc = mix2(acc, 907656192, 138230784)
    acc = acc % 2147483647
    acc = mix2(acc, 307936320, 259905408)
    acc = mix2(acc, 490598912, 1058814464)
    acc = mix2(acc, 347136512, 2910720)
    acc = acc % 2147483647
    acc = mix2(acc, 389370368, 212867584)
    acc = mix2(acc, 719798784, 110316032)
    acc = mix2(acc, 692236864, 20288768)
    acc = acc % 2147483647
    acc = mix2(acc, 355901696, 860929280)
    acc = mix2(acc, 553036032, 646194432)
    acc = mix2(acc, 126480640, 589942016)
    acc = acc % 2147483647
    acc = mix2(acc, 54688000, 975942968)
    acc = mix2(acc, 925067520, 974251264)
    acc = mix2(acc, 191148288, 768445696)
    acc = acc % 2147483647
    return acc
end

local function block23(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 796604672, 416642304)
    acc = mix2(acc, 445186304, 279158016)
    acc = mix2(acc, 494930240, 970301440)
    acc = acc % 2147483647
    acc = mix2(acc, 917852160, 697516032)
    acc = mix2(acc, 288518144, 278519872)
    acc = mix2(acc, 626535168, 94666496)
    acc = acc % 2147483647
    acc = mix2(acc, 1069302528, 294828032)
    acc = mix2(acc, 1065912384, 136688640)
    acc = mix2(acc, 753259584, 235343616)
    acc = acc % 2147483647
    acc = mix2(acc, 965972800, 506683904)
    acc = mix2(acc, 264038976, 810511616)
    acc = mix2(acc, 319265024, 14308608)
    acc = acc % 2147483647
    acc = mix2(acc, 551313720, 214213888)
    acc = mix2(acc, 939553088, 305260544)
    acc = mix2(acc, 602929216, 743465728)
    acc = acc % 2147483647
    acc = mix2(acc, 778327808, 490420992)
    acc = mix2(acc, 540168000, 353463808)
    acc = mix2(acc, 468715072, 479877376)
    acc = acc % 2147483647
    return acc
end

local function block24(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 181802240, 361570560)
    acc = mix2(acc, 858078464, 225519872)
    acc = mix2(acc, 699180352, 677121024)
    acc = acc % 2147483647
    acc = mix2(acc, 47795200, 867011640)
    acc = mix2(acc, 216921088, 967382080)
    acc = mix2(acc, 984174336, 707318528)
    acc = acc % 2147483647
    acc = mix2(acc, 363595520, 241249024)
    acc = mix2(acc, 1014595328, 1047347968)
    acc = mix2(acc, 821016576, 807503872)
    acc = acc % 2147483647
    acc = mix2(acc, 632756224, 675637248)
    acc = mix2(acc, 993884160, 62670848)
    acc = mix2(acc, 622516280, 765415424)
    acc = acc % 2147483647
    acc = mix2(acc, 389365760, 455897152)
    acc = mix2(acc, 765426560, 159958400)
    acc = mix2(acc, 1013532608, 187710720)
    acc = acc % 2147483647
    acc = mix2(acc, 606087424, 607102208)
    acc = mix2(acc, 177172736, 427904320)
    acc = mix2(acc, 18322560, 21588480)
    acc = acc % 2147483647
    return acc
end

local function block25(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 646861312, 865409536)
    acc = mix2(acc, 6832640, 1015040569)
    acc = mix2(acc, 978528256, 462960640)
    acc = acc % 2147483647
    acc = mix2(acc, 819988480, 30644224)
    acc = mix2(acc, 258467840, 669907008)
    acc = mix2(acc, 58090240, 496129792)
    acc = acc % 2147483647
    acc = mix2(acc, 936459008, 638603008)
    acc = mix2(acc, 117691136, 418055936)
    acc = mix2(acc, 573844288, 686037504)
    acc = acc % 2147483647
    acc = mix2(acc, 84623872, 535251520)
    acc = mix2(acc, 999968000, 841545984)
    acc = mix2(acc, 760231168, 961331456)
    acc = acc % 2147483647
    acc = mix2(acc, 243031296, 1028245824)
    acc = mix2(acc, 993812480, 502599680)
    acc = mix2(acc, 204996608, 64499776)
    acc = acc % 2147483647
    acc = mix2(acc, 322976512, 642993920)
    acc = mix2(acc, 227201792, 40240896)
    acc = mix2(acc, 93530936, 472164128)
    acc = acc % 2147483647
    return acc
end

local function block26(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 155654400, 853792000)
    acc = mix2(acc, 333963520, 642317568)
    acc = mix2(acc, 942270720, 618030336)
    acc = acc % 2147483647
    acc = mix2(acc, 537866496, 761662720)
    acc = mix2(acc, 980007168, 727840000)
    acc = mix2(acc, 257655040, 826547520)
    acc = acc % 2147483647
    acc = mix2(acc, 443163648, 774472768)
    acc = mix2(acc, 393805568, 372687680)
    acc = mix2(acc, 676254336, 1018351616)
    acc = acc % 2147483647
    acc = mix2(acc, 864206848, 86765568)
    acc = mix2(acc, 919953408, 471470080)
    acc = mix2(acc, 133758976, 50671680)
    acc = acc % 2147483647
    acc = mix2(acc, 795425536, 566775552)
    acc = mix2(acc, 73126656, 123930368)
    acc = mix2(acc, 994847488, 914915072)
    acc = acc % 2147483647
    acc = mix2(acc, 147866368, 118929152)
    acc = mix2(acc, 980294400, 253472512)
    acc = mix2(acc, 941339392, 106260224)
    acc = acc % 2147483647
    return acc
end

local function block27(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 566168384, 121993728)
    acc = mix2(acc, 532750848, 697805312)
    acc = mix2(acc, 660574720, 237853184)
    acc = acc % 2147483647
    acc = mix2(acc, 918258176, 997362176)
    acc = mix2(acc, 835777024, 196295168)
    acc = mix2(acc, 855368256, 833553664)
    acc = acc % 2147483647
    acc = mix2(acc, 420172032, 41921856)
    acc = mix2(acc, 916878336, 876057600)
    acc = mix2(acc, 583152640, 814240768)
    acc = acc % 2147483647
    acc = mix2(acc, 732165120, 19338240)
    acc = mix2(acc, 498840632, 891616256)
    acc = mix2(acc, 226982912, 846724096)
    acc = acc % 2147483647
    acc = mix2(acc, 870218752, 126024704)
    acc = mix2(acc, 59604032, 949589880)
    acc = mix2(acc, 73154048, 707433984)
    acc = acc % 2147483647
    acc = mix2(acc, 836111872, 73934336)
    acc = mix2(acc, 1072455232, 568054272)
    acc = mix2(acc, 278174208, 842121728)
    acc = acc % 2147483647
    return acc
end

local function block28(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 340511232, 936456768)
    acc = mix2(acc, 518132992, 111747328)
    acc = mix2(acc, 605109568, 998294528)
    acc = acc % 2147483647
    acc = mix2(acc, 697017344, 342688768)
    acc = mix2(acc, 27452416, 735265792)
    acc = mix2(acc, 531375104, 121865216)
    acc = acc % 2147483647
    acc = mix2(acc, 32990272, 6381312)
    acc = mix2(acc, 241672960, 130717504)
    acc = mix2(acc, 114824704, 79007296)
    acc = acc % 2147483647
    acc = mix2(acc, 405094656, 665509120)
    acc = mix2(acc, 555108608, 381371648)
    acc = mix2(acc, 448958784, 77170816)
    acc = acc % 2147483647
    acc = mix2(acc, 267800064, 732128832)
    acc = mix2(acc, 859453696, 589853952)
    acc = mix2(acc, 165675264, 984521984)
    acc = acc % 2147483647
    acc = mix2(acc, 506398976, 616891648)
    acc = mix2(acc, 225005824, 847621376)
    acc = mix2(acc, 195090688, 194680128)
    acc = acc % 2147483647
    return acc
end

local function block29(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 969620480, 802208768)
    acc = mix2(acc, 489176064, 342473728)
    acc = mix2(acc, 273497088, 556448832)
    acc = acc % 2147483647
    acc = mix2(acc, 1041094400, 761707264)
    acc = mix2(acc, 62377728, 810898232)
    acc = mix2(acc, 687845120, 909095680)
    acc = acc % 2147483647
    acc = mix2(acc, 215574272, 536021824)
    acc = mix2(acc, 459897344, 722805248)
    acc = mix2(acc, 341551616, 349409792)
    acc = acc % 2147483647
    acc = mix2(acc, 9525760, 592413240)
    acc = mix2(acc, 836219392, 487782912)
    acc = mix2(acc, 88121920, 729073024)
    acc = acc % 2147483647
    acc = mix2(acc, 367347584, 996712960)
    acc = mix2(acc, 954804736, 34804224)
    acc = mix2(acc, 496687672, 146549248)
    acc = acc % 2147483647
    acc = mix2(acc, 149557760, 153475584)
    acc = mix2(acc, 160039424, 677317120)
    acc = mix2(acc, 562262528, 1035188736)
    acc = acc % 2147483647
    return acc
end

local function block30(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 607281664, 256895488)
    acc = mix2(acc, 441635392, 248722688)
    acc = mix2(acc, 672383296, 214628352)
    acc = acc % 2147483647
    acc = mix2(acc, 435931136, 925128704)
    acc = mix2(acc, 491366400, 428652608)
    acc = mix2(acc, 42869504, 925832960)
    acc = acc % 2147483647
    acc = mix2(acc, 571407104, 853217024)
    acc = mix2(acc, 1071092480, 261343232)
    acc = mix2(acc, 417093632, 144812096)
    acc = acc % 2147483647
    acc = mix2(acc, 261640960, 1059424064)
    acc = mix2(acc, 766526976, 224525824)
    acc = mix2(acc, 35476992, 447860224)
    acc = acc % 2147483647
    acc = mix2(acc, 511239744, 622967040)
    acc = mix2(acc, 733607168, 80969984)
    acc = mix2(acc, 89922880, 950150144)
    acc = acc % 2147483647
    acc = mix2(acc, 469861376, 987587648)
    acc = mix2(acc, 240082688, 12428096)
    acc = mix2(acc, 469512704, 765295104)
    acc = acc % 2147483647
    return acc
end

local function block31(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 54075904, 330279424)
    acc = mix2(acc, 597135872, 33905152)
    acc = mix2(acc, 431194624, 789219840)
    acc = acc % 2147483647
    acc = mix2(acc, 965276160, 621492736)
    acc = mix2(acc, 225698304, 355691008)
    acc = mix2(acc, 758141504, 337818880)
    acc = acc % 2147483647
    acc = mix2(acc, 602751232, 466803968)
    acc = mix2(acc, 102648128, 1018617984)
    acc = mix2(acc, 893699712, 700691584)
    acc = acc % 2147483647
    acc = mix2(acc, 72551040, 59680960)
    acc = mix2(acc, 1068009984, 818868736)
    acc = mix2(acc, 31013376, 355066368)
    acc = acc % 2147483647
    acc = mix2(acc, 296839744, 74050944)
    acc = mix2(acc, 410472384, 865707264)
    acc = mix2(acc, 649163008, 804998400)
    acc = acc % 2147483647
    acc = mix2(acc, 225835264, 51983616)
    acc = mix2(acc, 1038205240, 189694976)
    acc = mix2(acc, 346448960, 115629824)
    acc = acc % 2147483647
    return acc
end

local function block32(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 1043243840, 857289216)
    acc = mix2(acc, 452360704, 807281152)
    acc = mix2(acc, 925936128, 260346368)
    acc = acc % 2147483647
    acc = mix2(acc, 737040960, 561782016)
    acc = mix2(acc, 11438336, 772703544)
    acc = mix2(acc, 169212160, 322516288)
    acc = acc % 2147483647
    acc = mix2(acc, 219503744, 524784320)
    acc = mix2(acc, 548416512, 543878144)
    acc = mix2(acc, 695118848, 612854784)
    acc = acc % 2147483647
    acc = mix2(acc, 412167168, 959966272)
    acc = mix2(acc, 541804288, 430926592)
    acc = mix2(acc, 608878336, 872776448)
    acc = acc % 2147483647
    acc = mix2(acc, 641922816, 1002789632)
    acc = mix2(acc, 918679296, 23393024)
    acc = mix2(acc, 201697080, 615215872)
    acc = acc % 2147483647
    acc = mix2(acc, 201991936, 573885184)
    acc = mix2(acc, 676634368, 779739904)
    acc = mix2(acc, 1058002688, 926989056)
    acc = acc % 2147483647
    return acc
end

local function block33(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 562679552, 515855104)
    acc = mix2(acc, 348161792, 689679104)
    acc = mix2(acc, 820117248, 636207872)
    acc = acc % 2147483647
    acc = mix2(acc, 214710016, 901094208)
    acc = mix2(acc, 119589376, 42086976)
    acc = mix2(acc, 440341760, 591536448)
    acc = acc % 2147483647
    acc = mix2(acc, 550098944, 96544768)
    acc = mix2(acc, 1030633536, 1049731840)
    acc = mix2(acc, 363632640, 550299648)
    acc = acc % 2147483647
    acc = mix2(acc, 367982592, 911198208)
    acc = mix2(acc, 518543360, 62761024)
    acc = mix2(acc, 42226552, 714492416)
    acc = acc % 2147483647
    acc = mix2(acc, 680112640, 584907264)
    acc = mix2(acc, 138584576, 498805248)
    acc = mix2(acc, 729616896, 196925952)
    acc = acc % 2147483647
    acc = mix2(acc, 634716672, 239995392)
    acc = mix2(acc, 440824320, 314227200)
    acc = mix2(acc, 976052800, 381846784)
    acc = acc % 2147483647
    return acc
end

local function block34(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 324587776, 847195456)
    acc = mix2(acc, 1064301568, 386077696)
    acc = mix2(acc, 879211520, 553636864)
    acc = acc % 2147483647
    acc = mix2(acc, 931492864, 86676480)
    acc = mix2(acc, 607728704, 63368960)
    acc = mix2(acc, 310768440, 141807360)
    acc = acc % 2147483647
    acc = mix2(acc, 1013011264, 5120512)
    acc = mix2(acc, 1000385024, 322212352)
    acc = mix2(acc, 501111360, 78388608)
    acc = acc % 2147483647
    acc = mix2(acc, 1061535680, 1067406592)
    acc = mix2(acc, 549282048, 707820800)
    acc = mix2(acc, 716925184, 30008576)
    acc = acc % 2147483647
    acc = mix2(acc, 616345912, 732345600)
    acc = mix2(acc, 522273024, 687267072)
    acc = mix2(acc, 376588544, 384255232)
    acc = acc % 2147483647
    acc = mix2(acc, 360616256, 245729408)
    acc = mix2(acc, 1058164224, 42952192)
    acc = mix2(acc, 740466232, 776117760)
    acc = acc % 2147483647
    return acc
end

local function block35(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 847345152, 924020224)
    acc = mix2(acc, 852100608, 299417088)
    acc = mix2(acc, 388830720, 518426112)
    acc = acc % 2147483647
    acc = mix2(acc, 997107200, 682679808)
    acc = mix2(acc, 720549376, 13348352)
    acc = mix2(acc, 968705536, 619380224)
    acc = acc % 2147483647
    acc = mix2(acc, 91379200, 736284224)
    acc = mix2(acc, 266961152, 834280768)
    acc = mix2(acc, 11019264, 445613056)
    acc = acc % 2147483647
    acc = mix2(acc, 559113280, 579690240)
    acc = mix2(acc, 52667136, 64395064)
    acc = mix2(acc, 834773760, 18650880)
    acc = acc % 2147483647
    acc = mix2(acc, 959347512, 128300800)
    acc = mix2(acc, 156310272, 264417088)
    acc = mix2(acc, 315248256, 453942272)
    acc = acc % 2147483647
    acc = mix2(acc, 549493824, 991835904)
    acc = mix2(acc, 715102976, 690958080)
    acc = mix2(acc, 57225984, 861202232)
    acc = acc % 2147483647
    return acc
end

local function block36(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 115979008, 403400512)
    acc = mix2(acc, 298886784, 798102528)
    acc = mix2(acc, 994751488, 967234560)
    acc = acc % 2147483647
    acc = mix2(acc, 413168640, 396989440)
    acc = mix2(acc, 910963712, 239752192)
    acc = mix2(acc, 859698176, 699995136)
    acc = acc % 2147483647
    acc = mix2(acc, 535147520, 161862656)
    acc = mix2(acc, 439579648, 222336000)
    acc = mix2(acc, 160904192, 523850816)
    acc = acc % 2147483647
    acc = mix2(acc, 747933440, 416486144)
    acc = mix2(acc, 329860928, 239666816)
    acc = mix2(acc, 471004160, 845812800)
    acc = acc % 2147483647
    acc = mix2(acc, 852905728, 189911808)
    acc = mix2(acc, 841025280, 305277696)
    acc = mix2(acc, 175537920, 153502528)
    acc = acc % 2147483647
    acc = mix2(acc, 289952256, 281483840)
    acc = mix2(acc, 474340608, 18172224)
    acc = mix2(acc, 434233344, 392990784)
    acc = acc % 2147483647
    return acc
end

local function block37(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 514661248, 320202240)
    acc = mix2(acc, 812064256, 990639616)
    acc = mix2(acc, 218885632, 77104640)
    acc = acc % 2147483647
    acc = mix2(acc, 298139200, 259207552)
    acc = mix2(acc, 908149696, 357468416)
    acc = mix2(acc, 909018432, 233335808)
    acc = acc % 2147483647
    acc = mix2(acc, 81669120, 201387072)
    acc = mix2(acc, 501237504, 941648704)
    acc = mix2(acc, 972773888, 561086976)
    acc = acc % 2147483647
    acc = mix2(acc, 519080448, 846656064)
    acc = mix2(acc, 401921280, 170189056)
    acc = mix2(acc, 339868928, 690940160)
    acc = acc % 2147483647
    acc = mix2(acc, 167208192, 1025257792)
    acc = mix2(acc, 864200704, 768718848)
    acc = mix2(acc, 945522688, 308877312)
    acc = acc % 2147483647
    acc = mix2(acc, 739207168, 608139264)
    acc = mix2(acc, 362170368, 720164864)
    acc = mix2(acc, 11020288, 868825088)
    acc = acc % 2147483647
    return acc
end

local function block38(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 733808640, 669816832)
    acc = mix2(acc, 638380032, 305068032)
    acc = mix2(acc, 495962112, 354965568)
    acc = acc % 2147483647
    acc = mix2(acc, 244401024, 728428032)
    acc = mix2(acc, 621512192, 750534144)
    acc = mix2(acc, 453678592, 20319744)
    acc = acc % 2147483647
    acc = mix2(acc, 273163832, 833455616)
    acc = mix2(acc, 699809280, 1031575040)
    acc = mix2(acc, 666897920, 84551168)
    acc = acc % 2147483647
    acc = mix2(acc, 551968320, 407752960)
    acc = mix2(acc, 886058240, 945379584)
    acc = mix2(acc, 220796160, 484712704)
    acc = acc % 2147483647
    acc = mix2(acc, 355482944, 397657088)
    acc = mix2(acc, 893559872, 82550528)
    acc = mix2(acc, 482665280, 9597440)
    acc = acc % 2147483647
    acc = mix2(acc, 152484408, 179997216)
    acc = mix2(acc, 474136544, 639771136)
    acc = mix2(acc, 786737664, 522752512)
    acc = acc % 2147483647
    return acc
end

local function block39(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 214063616, 36471360)
    acc = mix2(acc, 975381760, 339538176)
    acc = mix2(acc, 358665536, 406918272)
    acc = acc % 2147483647
    acc = mix2(acc, 49605312, 844014584)
    acc = mix2(acc, 725717120, 670152320)
    acc = mix2(acc, 243655808, 899774976)
    acc = acc % 2147483647
    acc = mix2(acc, 619520512, 89369088)
    acc = mix2(acc, 1047237120, 105470464)
    acc = mix2(acc, 581403200, 142746880)
    acc = acc % 2147483647
    acc = mix2(acc, 615510272, 542625024)
    acc = mix2(acc, 332953856, 1040823616)
    acc = mix2(acc, 249289728, 950328384)
    acc = acc % 2147483647
    acc = mix2(acc, 852326144, 96328448)
    acc = mix2(acc, 747663104, 357659392)
    acc = mix2(acc, 381167424, 601777664)
    acc = acc % 2147483647
    acc = mix2(acc, 751148544, 977827328)
    acc = mix2(acc, 684617216, 426313216)
    acc = mix2(acc, 898350592, 342103552)
    acc = acc % 2147483647
    return acc
end

local function block40(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 827428352, 754820608)
    acc = mix2(acc, 345234944, 77182528)
    acc = mix2(acc, 8014208, 259423161)
    acc = acc % 2147483647
    acc = mix2(acc, 423312384, 847297600)
    acc = mix2(acc, 330028800, 95206208)
    acc = mix2(acc, 521759360, 457541824)
    acc = acc % 2147483647
    acc = mix2(acc, 281405952, 764465728)
    acc = mix2(acc, 607728896, 411156736)
    acc = mix2(acc, 1041063168, 738641920)
    acc = acc % 2147483647
    acc = mix2(acc, 1070815232, 567060480)
    acc = mix2(acc, 794015744, 951420928)
    acc = mix2(acc, 616241152, 888760320)
    acc = acc % 2147483647
    acc = mix2(acc, 1004632064, 26232832)
    acc = mix2(acc, 274651136, 775039040)
    acc = mix2(acc, 354341632, 342492992)
    acc = acc % 2147483647
    acc = mix2(acc, 450545280, 513913856)
    acc = mix2(acc, 129094656, 760181760)
    acc = mix2(acc, 942445568, 940782592)
    acc = acc % 2147483647
    return acc
end

local function block41(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 839209984, 601682944)
    acc = mix2(acc, 258741248, 924628032)
    acc = mix2(acc, 530870016, 752186112)
    acc = acc % 2147483647
    acc = mix2(acc, 300689152, 1064225536)
    acc = mix2(acc, 101097472, 361910336)
    acc = mix2(acc, 356506368, 585789184)
    acc = acc % 2147483647
    acc = mix2(acc, 631468800, 94512896)
    acc = mix2(acc, 938265344, 934059776)
    acc = mix2(acc, 116515584, 975689536)
    acc = acc % 2147483647
    acc = mix2(acc, 571233792, 91540992)
    acc = mix2(acc, 1031792128, 193535488)
    acc = mix2(acc, 612759040, 570289664)
    acc = acc % 2147483647
    acc = mix2(acc, 732071424, 1023884800)
    acc = mix2(acc, 620336640, 234425856)
    acc = mix2(acc, 693052992, 165345536)
    acc = acc % 2147483647
    acc = mix2(acc, 1717568, 920804473)
    acc = mix2(acc, 354708992, 772212224)
    acc = mix2(acc, 550616576, 355603968)
    acc = acc % 2147483647
    return acc
end

local function block42(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 218598912, 763078208)
    acc = mix2(acc, 533559552, 995800320)
    acc = mix2(acc, 650461440, 566942976)
    acc = acc % 2147483647
    acc = mix2(acc, 548817152, 769344768)
    acc = mix2(acc, 862097664, 254143744)
    acc = mix2(acc, 257694016, 560492544)
    acc = acc % 2147483647
    acc = mix2(acc, 731373568, 367693824)
    acc = mix2(acc, 750747712, 404572928)
    acc = mix2(acc, 861085440, 668361472)
    acc = acc % 2147483647
    acc = mix2(acc, 443700992, 590428992)
    acc = mix2(acc, 260303360, 142004736)
    acc = mix2(acc, 982751808, 928537856)
    acc = acc % 2147483647
    acc = mix2(acc, 720750848, 602195200)
    acc = mix2(acc, 443420928, 806055168)
    acc = mix2(acc, 1041471744, 1022776576)
    acc = acc % 2147483647
    acc = mix2(acc, 13132032, 685735168)
    acc = mix2(acc, 759064832, 1032905984)
    acc = mix2(acc, 980922368, 1016471552)
    acc = acc % 2147483647
    return acc
end

local function block43(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 162254848, 394772544)
    acc = mix2(acc, 316705536, 905273152)
    acc = mix2(acc, 671038976, 9541120)
    acc = acc % 2147483647
    acc = mix2(acc, 498142776, 235425280)
    acc = mix2(acc, 357393920, 185114176)
    acc = mix2(acc, 946016640, 981705728)
    acc = acc % 2147483647
    acc = mix2(acc, 503645184, 660268096)
    acc = mix2(acc, 1019141888, 1060578304)
    acc = mix2(acc, 259163136, 268067904)
    acc = acc % 2147483647
    acc = mix2(acc, 564682624, 1053415936)
    acc = mix2(acc, 408814080, 279120384)
    acc = mix2(acc, 1048015360, 697809408)
    acc = acc % 2147483647
    acc = mix2(acc, 205939200, 936634880)
    acc = mix2(acc, 310825472, 593730048)
    acc = mix2(acc, 106217984, 288541248)
    acc = acc % 2147483647
    acc = mix2(acc, 968871296, 944222208)
    acc = mix2(acc, 774250496, 66834432)
    acc = mix2(acc, 194494464, 740094016)
    acc = acc % 2147483647
    return acc
end

local function block44(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 721812224, 101060352)
    acc = mix2(acc, 52859648, 171362048)
    acc = mix2(acc, 871689024, 846584320)
    acc = acc % 2147483647
    acc = mix2(acc, 10093056, 976161336)
    acc = mix2(acc, 980720128, 110215680)
    acc = mix2(acc, 19647040, 80006400)
    acc = acc % 2147483647
    acc = mix2(acc, 205617408, 12498176)
    acc = mix2(acc, 710492472, 590349568)
    acc = mix2(acc, 989352192, 711503104)
    acc = acc % 2147483647
    acc = mix2(acc, 21485824, 95786240)
    acc = mix2(acc, 1068933440, 1029986304)
    acc = mix2(acc, 109684736, 701282368)
    acc = acc % 2147483647
    acc = mix2(acc, 768484096, 560928512)
    acc = mix2(acc, 525269760, 119213824)
    acc = mix2(acc, 521638656, 500418368)
    acc = acc % 2147483647
    acc = mix2(acc, 358712960, 411137216)
    acc = mix2(acc, 758212096, 790404608)
    acc = mix2(acc, 995325440, 880635392)
    acc = acc % 2147483647
    return acc
end

local function block45(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 619447808, 106085888)
    acc = mix2(acc, 455022080, 149982784)
    acc = mix2(acc, 524566912, 84218880)
    acc = acc % 2147483647
    acc = mix2(acc, 658617408, 627404544)
    acc = mix2(acc, 771912448, 135603968)
    acc = mix2(acc, 216255296, 409680512)
    acc = acc % 2147483647
    acc = mix2(acc, 276502528, 364472384)
    acc = mix2(acc, 523572096, 176848256)
    acc = mix2(acc, 3448768, 764720384)
    acc = acc % 2147483647
    acc = mix2(acc, 897007872, 529849600)
    acc = mix2(acc, 1001933056, 162874624)
    acc = mix2(acc, 993300736, 529526016)
    acc = acc % 2147483647
    acc = mix2(acc, 410917120, 793695552)
    acc = mix2(acc, 420117504, 54459392)
    acc = mix2(acc, 983137336, 42148864)
    acc = acc % 2147483647
    acc = mix2(acc, 516850688, 572941376)
    acc = mix2(acc, 107241216, 128885568)
    acc = mix2(acc, 1050227200, 7764480)
    acc = acc % 2147483647
    return acc
end

local function block46(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 664674873, 1037574272)
    acc = mix2(acc, 300746240, 1035976256)
    acc = mix2(acc, 471577088, 336001536)
    acc = acc % 2147483647
    acc = mix2(acc, 52732416, 200616448)
    acc = mix2(acc, 103748160, 387609856)
    acc = mix2(acc, 602538304, 243546112)
    acc = acc % 2147483647
    acc = mix2(acc, 123471936, 238236416)
    acc = mix2(acc, 391571264, 100017792)
    acc = mix2(acc, 277451968, 40306176)
    acc = acc % 2147483647
    acc = mix2(acc, 229752376, 334708256)
    acc = mix2(acc, 138372096, 729138240)
    acc = mix2(acc, 745553664, 419234560)
    acc = acc % 2147483647
    acc = mix2(acc, 212105024, 970749568)
    acc = mix2(acc, 905552896, 591594496)
    acc = mix2(acc, 113304576, 1033454592)
    acc = acc % 2147483647
    acc = mix2(acc, 83502080, 762938432)
    acc = mix2(acc, 747175680, 772195072)
    acc = mix2(acc, 978007808, 113810176)
    acc = acc % 2147483647
    return acc
end

local function block47(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 614739776, 734907904)
    acc = mix2(acc, 795141632, 269098496)
    acc = mix2(acc, 494073344, 350678528)
    acc = acc % 2147483647
    acc = mix2(acc, 383223360, 975952128)
    acc = mix2(acc, 919180544, 1027252480)
    acc = mix2(acc, 889502976, 848996608)
    acc = acc % 2147483647
    acc = mix2(acc, 559424768, 734916864)
    acc = mix2(acc, 203279616, 187070720)
    acc = mix2(acc, 239056192, 702143616)
    acc = acc % 2147483647
    acc = mix2(acc, 1039274496, 227400192)
    acc = mix2(acc, 433193472, 117901824)
    acc = mix2(acc, 520843776, 547076672)
    acc = acc % 2147483647
    acc = mix2(acc, 579730688, 663415040)
    acc = mix2(acc, 522413312, 685256960)
    acc = mix2(acc, 687541504, 1054521600)
    acc = acc % 2147483647
    acc = mix2(acc, 1031730176, 359011328)
    acc = mix2(acc, 781104192, 925789952)
    acc = mix2(acc, 1050112768, 1032201984)
    acc = acc % 2147483647
    return acc
end

local function block48(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 1006684928, 204513024)
    acc = mix2(acc, 992338688, 206296832)
    acc = mix2(acc, 567065344, 656789248)
    acc = acc % 2147483647
    acc = mix2(acc, 170611456, 968656640)
    acc = mix2(acc, 812100352, 876478208)
    acc = mix2(acc, 471319296, 92813120)
    acc = acc % 2147483647
    acc = mix2(acc, 391460480, 216228864)
    acc = mix2(acc, 491373568, 169911296)
    acc = mix2(acc, 433980416, 12575744)
    acc = acc % 2147483647
    acc = mix2(acc, 556549176, 401755136)
    acc = mix2(acc, 223513664, 207709952)
    acc = mix2(acc, 484117248, 144563968)
    acc = acc % 2147483647
    acc = mix2(acc, 1059726080, 228268032)
    acc = mix2(acc, 475621440, 827659136)
    acc = mix2(acc, 36339072, 794075064)
    acc = acc % 2147483647
    acc = mix2(acc, 805454080, 650370304)
    acc = mix2(acc, 482035968, 33310016)
    acc = mix2(acc, 84093952, 566356992)
    acc = acc % 2147483647
    return acc
end

local function block49(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 1031384064, 121007104)
    acc = mix2(acc, 789851136, 956230656)
    acc = mix2(acc, 947039232, 20671488)
    acc = acc % 2147483647
    acc = mix2(acc, 691350584, 711791616)
    acc = mix2(acc, 76133376, 460968960)
    acc = mix2(acc, 158823424, 624202816)
    acc = acc % 2147483647
    acc = mix2(acc, 101633792, 828396288)
    acc = mix2(acc, 290293504, 656714496)
    acc = mix2(acc, 414646016, 27050816)
    acc = acc % 2147483647
    acc = mix2(acc, 918344312, 391535872)
    acc = mix2(acc, 773572928, 10903552)
    acc = mix2(acc, 941035576, 739452928)
    acc = acc % 2147483647
    acc = mix2(acc, 173553664, 413141056)
    acc = mix2(acc, 539440000, 929177088)
    acc = mix2(acc, 770373120, 671018496)
    acc = acc % 2147483647
    acc = mix2(acc, 135235072, 225459776)
    acc = mix2(acc, 289550592, 706547008)
    acc = mix2(acc, 132666368, 528741440)
    acc = acc % 2147483647
    return acc
end

local function block50(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 152743680, 464433920)
    acc = mix2(acc, 904685376, 412984832)
    acc = mix2(acc, 666099200, 691648000)
    acc = acc % 2147483647
    acc = mix2(acc, 654749184, 987514368)
    acc = mix2(acc, 287178240, 556357120)
    acc = mix2(acc, 986566144, 308602368)
    acc = acc % 2147483647
    acc = mix2(acc, 923409984, 705073408)
    acc = mix2(acc, 184151296, 547363072)
    acc = mix2(acc, 29938944, 828956928)
    acc = acc % 2147483647
    acc = mix2(acc, 70647040, 739436800)
    acc = mix2(acc, 1024156928, 346042624)
    acc = mix2(acc, 1025707328, 896947200)
    acc = acc % 2147483647
    acc = mix2(acc, 150598656, 851776576)
    acc = mix2(acc, 328552192, 782553920)
    acc = mix2(acc, 940286464, 877668864)
    acc = acc % 2147483647
    acc = mix2(acc, 787354112, 819583488)
    acc = mix2(acc, 154010112, 959646208)
    acc = mix2(acc, 600273408, 749189632)
    acc = acc % 2147483647
    return acc
end

local function block51(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 974545408, 425978368)
    acc = mix2(acc, 1020711488, 956773632)
    acc = mix2(acc, 943177984, 847994112)
    acc = acc % 2147483647
    acc = mix2(acc, 699189504, 433046784)
    acc = mix2(acc, 696062208, 728657152)
    acc = mix2(acc, 825923840, 647058688)
    acc = acc % 2147483647
    acc = mix2(acc, 835150080, 930249984)
    acc = mix2(acc, 735406336, 635168000)
    acc = mix2(acc, 1013362944, 665292032)
    acc = acc % 2147483647
    acc = mix2(acc, 1028471040, 926905600)
    acc = mix2(acc, 430637312, 236821760)
    acc = mix2(acc, 913443136, 350199808)
    acc = acc % 2147483647
    acc = mix2(acc, 100094016, 910220032)
    acc = mix2(acc, 1045917440, 151811072)
    acc = mix2(acc, 497390592, 177747008)
    acc = acc % 2147483647
    acc = mix2(acc, 205123456, 413411776)
    acc = mix2(acc, 220100352, 674945856)
    acc = mix2(acc, 593299968, 598307328)
    acc = acc % 2147483647
    return acc
end

local function block52(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 1004648960, 566780416)
    acc = mix2(acc, 1009641984, 416862720)
    acc = mix2(acc, 273521152, 838255104)
    acc = acc % 2147483647
    acc = mix2(acc, 19712512, 564015616)
    acc = mix2(acc, 798456320, 111878656)
    acc = mix2(acc, 121219584, 559518272)
    acc = acc % 2147483647
    acc = mix2(acc, 456324352, 425230592)
    acc = mix2(acc, 134028544, 865831168)
    acc = mix2(acc, 318211328, 468308224)
    acc = acc % 2147483647
    acc = mix2(acc, 104607040, 1021899776)
    acc = mix2(acc, 562566144, 889762816)
    acc = mix2(acc, 864867328, 328102912)
    acc = acc % 2147483647
    acc = mix2(acc, 855610368, 233452544)
    acc = mix2(acc, 9458752, 547327864)
    acc = mix2(acc, 813094400, 466439680)
    acc = acc % 2147483647
    acc = mix2(acc, 942564928, 610875648)
    acc = mix2(acc, 640382208, 1009189120)
    acc = mix2(acc, 82418944, 860752128)
    acc = acc % 2147483647
    return acc
end

local function block53(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 351798528, 488657152)
    acc = mix2(acc, 628141376, 88306688)
    acc = mix2(acc, 51418112, 321336320)
    acc = acc % 2147483647
    acc = mix2(acc, 294212672, 950308608)
    acc = mix2(acc, 463672064, 641288000)
    acc = mix2(acc, 362350080, 905690688)
    acc = acc % 2147483647
    acc = mix2(acc, 363311360, 911510784)
    acc = mix2(acc, 854997248, 238318848)
    acc = mix2(acc, 100401408, 446340352)
    acc = acc % 2147483647
    acc = mix2(acc, 497748224, 881919296)
    acc = mix2(acc, 551087104, 474262528)
    acc = mix2(acc, 1034251264, 777464832)
    acc = acc % 2147483647
    acc = mix2(acc, 310766592, 955100160)
    acc = mix2(acc, 798649344, 430451712)
    acc = mix2(acc, 839052352, 683296512)
    acc = acc % 2147483647
    acc = mix2(acc, 49441536, 533839672)
    acc = mix2(acc, 300002048, 556793600)
    acc = mix2(acc, 992068352, 147470080)
    acc = acc % 2147483647
    return acc
end

local function block54(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 618371840, 1018900224)
    acc = mix2(acc, 1040528384, 906974208)
    acc = mix2(acc, 673117184, 998642688)
    acc = acc % 2147483647
    acc = mix2(acc, 707703808, 683332608)
    acc = mix2(acc, 1009021952, 786273280)
    acc = mix2(acc, 795882496, 446583808)
    acc = acc % 2147483647
    acc = mix2(acc, 184677440, 834711424)
    acc = mix2(acc, 562293248, 846647808)
    acc = mix2(acc, 479435264, 1069859392)
    acc = acc % 2147483647
    acc = mix2(acc, 393635072, 774844736)
    acc = mix2(acc, 580495360, 1016903680)
    acc = mix2(acc, 516589568, 28055552)
    acc = acc % 2147483647
    acc = mix2(acc, 899049472, 347764736)
    acc = mix2(acc, 871712768, 190830592)
    acc = mix2(acc, 463415360, 579326848)
    acc = acc % 2147483647
    acc = mix2(acc, 726023552, 614817664)
    acc = mix2(acc, 444783104, 69403200)
    acc = mix2(acc, 722969856, 727150848)
    acc = acc % 2147483647
    return acc
end

local function block55(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 1051282688, 294385664)
    acc = mix2(acc, 774424640, 903934720)
    acc = mix2(acc, 751937280, 539380480)
    acc = acc % 2147483647
    acc = mix2(acc, 489168640, 495411968)
    acc = mix2(acc, 617568000, 583678720)
    acc = mix2(acc, 269831936, 824496960)
    acc = acc % 2147483647
    acc = mix2(acc, 143368704, 986720832)
    acc = mix2(acc, 620866816, 309123328)
    acc = mix2(acc, 410424640, 997669888)
    acc = acc % 2147483647
    acc = mix2(acc, 235715584, 78920704)
    acc = mix2(acc, 319142976, 843896576)
    acc = mix2(acc, 507099904, 395235072)
    acc = acc % 2147483647
    acc = mix2(acc, 618681088, 1053656832)
    acc = mix2(acc, 111455232, 142961728)
    acc = mix2(acc, 21677952, 580919808)
    acc = acc % 2147483647
    acc = mix2(acc, 344631808, 987140672)
    acc = mix2(acc, 191624448, 1067286848)
    acc = mix2(acc, 183613440, 519800832)
    acc = acc % 2147483647
    return acc
end

local function block56(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 76093440, 61827072)
    acc = mix2(acc, 861894712, 697790464)
    acc = mix2(acc, 966451200, 925995008)
    acc = acc % 2147483647
    acc = mix2(acc, 972718080, 44609536)
    acc = mix2(acc, 661856312, 52819968)
    acc = mix2(acc, 951765048, 601391104)
    acc = acc % 2147483647
    acc = mix2(acc, 976138240, 528556032)
    acc = mix2(acc, 29020160, 132825144)
    acc = mix2(acc, 148183072, 953497088)
    acc = acc % 2147483647
    acc = mix2(acc, 758918656, 748930560)
    acc = mix2(acc, 202341888, 251450880)
    acc = mix2(acc, 786124352, 179831040)
    acc = acc % 2147483647
    acc = mix2(acc, 648453376, 650578176)
    acc = mix2(acc, 494732544, 63000896)
    acc = mix2(acc, 395397240, 297835328)
    acc = acc % 2147483647
    acc = mix2(acc, 298830336, 553988608)
    acc = mix2(acc, 275937792, 1038754368)
    acc = mix2(acc, 815809792, 658739456)
    acc = acc % 2147483647
    return acc
end

local function block57(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 871558400, 816126208)
    acc = mix2(acc, 434754816, 91611392)
    acc = mix2(acc, 62848256, 929556792)
    acc = acc % 2147483647
    acc = mix2(acc, 429755648, 923482432)
    acc = mix2(acc, 582553600, 195821568)
    acc = mix2(acc, 540815424, 567556864)
    acc = acc % 2147483647
    acc = mix2(acc, 861359872, 272552704)
    acc = mix2(acc, 17434368, 694627128)
    acc = mix2(acc, 896050944, 175196928)
    acc = acc % 2147483647
    acc = mix2(acc, 957816640, 98434560)
    acc = mix2(acc, 384390720, 253847936)
    acc = mix2(acc, 945757184, 587668480)
    acc = acc % 2147483647
    acc = mix2(acc, 1016011776, 192355328)
    acc = mix2(acc, 339680256, 23534656)
    acc = mix2(acc, 486717304, 215932480)
    acc = acc % 2147483647
    acc = mix2(acc, 136073600, 501474240)
    acc = mix2(acc, 267168000, 423765312)
    acc = mix2(acc, 939756672, 424804864)
    acc = acc % 2147483647
    return acc
end

local function block58(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 277285376, 713781824)
    acc = mix2(acc, 934237440, 529359104)
    acc = mix2(acc, 146832640, 235628864)
    acc = acc % 2147483647
    acc = mix2(acc, 476938368, 449242816)
    acc = mix2(acc, 964620288, 137125888)
    acc = mix2(acc, 2428992, 742103808)
    acc = acc % 2147483647
    acc = mix2(acc, 547041024, 73741056)
    acc = mix2(acc, 648079168, 343418368)
    acc = mix2(acc, 918314560, 409719040)
    acc = acc % 2147483647
    acc = mix2(acc, 630598912, 279402752)
    acc = mix2(acc, 710874432, 846576640)
    acc = mix2(acc, 57228288, 739687480)
    acc = acc % 2147483647
    acc = mix2(acc, 1045914624, 61719552)
    acc = mix2(acc, 448046136, 608381952)
    acc = mix2(acc, 805432320, 247049216)
    acc = acc % 2147483647
    acc = mix2(acc, 527854592, 39070720)
    acc = mix2(acc, 725543992, 18076672)
    acc = mix2(acc, 940143672, 415218688)
    acc = acc % 2147483647
    return acc
end

local function block59(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 485178432, 369383168)
    acc = mix2(acc, 1012611904, 308668928)
    acc = mix2(acc, 514904576, 491100736)
    acc = acc % 2147483647
    acc = mix2(acc, 958553344, 972540160)
    acc = mix2(acc, 599704832, 910168320)
    acc = mix2(acc, 74804480, 993368384)
    acc = acc % 2147483647
    acc = mix2(acc, 302247936, 1053646912)
    acc = mix2(acc, 38120448, 974285824)
    acc = mix2(acc, 515910656, 758836288)
    acc = acc % 2147483647
    acc = mix2(acc, 798115584, 1021995776)
    acc = mix2(acc, 510246656, 624460608)
    acc = mix2(acc, 344822272, 247683648)
    acc = acc % 2147483647
    acc = mix2(acc, 472718592, 738953536)
    acc = mix2(acc, 742072320, 418172928)
    acc = mix2(acc, 607436864, 780765952)
    acc = acc % 2147483647
    acc = mix2(acc, 988438272, 951886592)
    acc = mix2(acc, 872126208, 337738496)
    acc = mix2(acc, 666603328, 469872128)
    acc = acc % 2147483647
    return acc
end

local function block60(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 62604800, 168885760)
    acc = mix2(acc, 715150912, 906782976)
    acc = mix2(acc, 21423360, 49656120)
    acc = acc % 2147483647
    acc = mix2(acc, 669758720, 243417344)
    acc = mix2(acc, 591650048, 523979008)
    acc = mix2(acc, 310134016, 435130688)
    acc = acc % 2147483647
    acc = mix2(acc, 549619840, 191581824)
    acc = mix2(acc, 94084096, 885627968)
    acc = mix2(acc, 15939328, 206609152)
    acc = acc % 2147483647
    acc = mix2(acc, 797716224, 251802368)
    acc = mix2(acc, 24766272, 551364216)
    acc = mix2(acc, 414327040, 266747136)
    acc = acc % 2147483647
    acc = mix2(acc, 429795648, 6897792)
    acc = mix2(acc, 561489408, 411426304)
    acc = mix2(acc, 782480896, 19443200)
    acc = acc % 2147483647
    acc = mix2(acc, 928400952, 544286208)
    acc = mix2(acc, 767647232, 435762688)
    acc = mix2(acc, 26226240, 503013632)
    acc = acc % 2147483647
    return acc
end

local function block61(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 563510528, 355035392)
    acc = mix2(acc, 379577600, 937079104)
    acc = mix2(acc, 27207680, 519321656)
    acc = acc % 2147483647
    acc = mix2(acc, 174927872, 354263040)
    acc = mix2(acc, 73224256, 464444160)
    acc = mix2(acc, 841838336, 254440192)
    acc = acc % 2147483647
    acc = mix2(acc, 371009344, 668952064)
    acc = mix2(acc, 791846400, 951154176)
    acc = mix2(acc, 964914688, 477758976)
    acc = acc % 2147483647
    acc = mix2(acc, 835239488, 32551168)
    acc = mix2(acc, 258575672, 709386528)
    acc = mix2(acc, 220809856, 239593472)
    acc = acc % 2147483647
    acc = mix2(acc, 760084544, 223765248)
    acc = mix2(acc, 301094720, 910481024)
    acc = mix2(acc, 1001030784, 385456640)
    acc = acc % 2147483647
    acc = mix2(acc, 825410112, 659318016)
    acc = mix2(acc, 541929728, 734792960)
    acc = mix2(acc, 534231296, 523760896)
    acc = acc % 2147483647
    return acc
end

local function block62(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 360284416, 805969152)
    acc = mix2(acc, 925141248, 307004672)
    acc = mix2(acc, 958058752, 572075264)
    acc = acc % 2147483647
    acc = mix2(acc, 1047419136, 169481216)
    acc = mix2(acc, 926069760, 728683520)
    acc = mix2(acc, 986215424, 313627648)
    acc = acc % 2147483647
    acc = mix2(acc, 146027584, 103149440)
    acc = mix2(acc, 706750976, 947786240)
    acc = mix2(acc, 589945344, 356385280)
    acc = acc % 2147483647
    acc = mix2(acc, 1006832192, 669715712)
    acc = mix2(acc, 722123008, 770635008)
    acc = mix2(acc, 459571456, 253304128)
    acc = acc % 2147483647
    acc = mix2(acc, 874194048, 799914496)
    acc = mix2(acc, 396648960, 853142016)
    acc = mix2(acc, 135582208, 886676032)
    acc = acc % 2147483647
    acc = mix2(acc, 455499008, 766232832)
    acc = mix2(acc, 1063437568, 856952832)
    acc = mix2(acc, 1013645312, 328151040)
    acc = acc % 2147483647
    return acc
end

local function block63(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 345481216, 927944704)
    acc = mix2(acc, 388317184, 878473216)
    acc = mix2(acc, 360439808, 603885568)
    acc = acc % 2147483647
    acc = mix2(acc, 54755328, 884846648)
    acc = mix2(acc, 63260672, 653252664)
    acc = mix2(acc, 458248192, 965885952)
    acc = acc % 2147483647
    acc = mix2(acc, 314929152, 103866432)
    acc = mix2(acc, 950217472, 378765056)
    acc = mix2(acc, 125780736, 161068864)
    acc = acc % 2147483647
    acc = mix2(acc, 667465216, 468299264)
    acc = mix2(acc, 696468992, 272170496)
    acc = mix2(acc, 967333440, 208955648)
    acc = acc % 2147483647
    acc = mix2(acc, 999220480, 60666112)
    acc = mix2(acc, 1007848760, 742220032)
    acc = mix2(acc, 263224576, 574318848)
    acc = acc % 2147483647
    acc = mix2(acc, 592045312, 675066112)
    acc = mix2(acc, 96832768, 873675072)
    acc = mix2(acc, 253825024, 603193408)
    acc = acc % 2147483647
    return acc
end

local function block64(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 410503936, 752590592)
    acc = mix2(acc, 1039459072, 275699712)
    acc = mix2(acc, 352462848, 169556032)
    acc = acc % 2147483647
    acc = mix2(acc, 440050560, 1036570112)
    acc = mix2(acc, 289662464, 771563008)
    acc = mix2(acc, 669644288, 194113024)
    acc = acc % 2147483647
    acc = mix2(acc, 933660160, 314260992)
    acc = mix2(acc, 983406144, 778086656)
    acc = mix2(acc, 681977088, 201584896)
    acc = acc % 2147483647
    acc = mix2(acc, 924568832, 1028421888)
    acc = mix2(acc, 1013822720, 635191552)
    acc = mix2(acc, 9821440, 388319488)
    acc = acc % 2147483647
    acc = mix2(acc, 756958528, 1048390656)
    acc = mix2(acc, 112454656, 881044480)
    acc = mix2(acc, 41446400, 103689216)
    acc = acc % 2147483647
    acc = mix2(acc, 990964800, 128993024)
    acc = mix2(acc, 632318720, 246923008)
    acc = mix2(acc, 980321088, 277582336)
    acc = acc % 2147483647
    return acc
end

local function block65(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 1038703168, 56303104)
    acc = mix2(acc, 619705856, 455077376)
    acc = mix2(acc, 454854208, 294443264)
    acc = acc % 2147483647
    acc = mix2(acc, 957781312, 530004992)
    acc = mix2(acc, 799849472, 366230528)
    acc = mix2(acc, 497400832, 114900032)
    acc = acc % 2147483647
    acc = mix2(acc, 878335872, 1042313728)
    acc = mix2(acc, 42777088, 311910968)
    acc = mix2(acc, 967971328, 1045289472)
    acc = acc % 2147483647
    acc = mix2(acc, 462553600, 96930368)
    acc = mix2(acc, 140446976, 342800704)
    acc = mix2(acc, 924225664, 396539392)
    acc = acc % 2147483647
    acc = mix2(acc, 666611200, 770524672)
    acc = mix2(acc, 1029373440, 220354048)
    acc = mix2(acc, 299028032, 387547520)
    acc = acc % 2147483647
    acc = mix2(acc, 72438720, 614816000)
    acc = mix2(acc, 293934336, 1057088)
    acc = mix2(acc, 679467008, 37693440)
    acc = acc % 2147483647
    return acc
end

local function block66(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 588527616, 682362880)
    acc = mix2(acc, 732928000, 705947648)
    acc = mix2(acc, 724170752, 980338688)
    acc = acc % 2147483647
    acc = mix2(acc, 303781888, 443852864)
    acc = mix2(acc, 812602240, 534232448)
    acc = mix2(acc, 463003520, 825252352)
    acc = acc % 2147483647
    acc = mix2(acc, 151159296, 632130112)
    acc = mix2(acc, 411274496, 318323008)
    acc = mix2(acc, 185536640, 259077824)
    acc = acc % 2147483647
    acc = mix2(acc, 711131136, 908537856)
    acc = mix2(acc, 525312000, 396840960)
    acc = mix2(acc, 748503040, 678719488)
    acc = acc % 2147483647
    acc = mix2(acc, 330555392, 836145216)
    acc = mix2(acc, 701438720, 157855488)
    acc = mix2(acc, 14988032, 32139064)
    acc = acc % 2147483647
    acc = mix2(acc, 160510736, 516017664)
    acc = mix2(acc, 961078848, 800176384)
    acc = mix2(acc, 185201920, 971211072)
    acc = acc % 2147483647
    return acc
end

local function block67(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 784316416, 565282816)
    acc = mix2(acc, 537335808, 475359232)
    acc = mix2(acc, 101545984, 1045186560)
    acc = acc % 2147483647
    acc = mix2(acc, 879417344, 793651200)
    acc = mix2(acc, 611792896, 701696000)
    acc = mix2(acc, 189437952, 420669504)
    acc = acc % 2147483647
    acc = mix2(acc, 290493312, 20971008)
    acc = mix2(acc, 1000560128, 750767616)
    acc = mix2(acc, 309257728, 122428928)
    acc = acc % 2147483647
    acc = mix2(acc, 9238016, 855174656)
    acc = mix2(acc, 545359360, 838483456)
    acc = mix2(acc, 980456960, 866389504)
    acc = acc % 2147483647
    acc = mix2(acc, 220079616, 694836736)
    acc = mix2(acc, 1055798784, 601959936)
    acc = mix2(acc, 920962560, 935099904)
    acc = acc % 2147483647
    acc = mix2(acc, 497407488, 718294592)
    acc = mix2(acc, 940114176, 539204864)
    acc = mix2(acc, 922749184, 599917824)
    acc = acc % 2147483647
    return acc
end

local function block68(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 891441408, 977972480)
    acc = mix2(acc, 545380608, 1030198528)
    acc = mix2(acc, 847290368, 830754816)
    acc = acc % 2147483647
    acc = mix2(acc, 95830016, 907636800)
    acc = mix2(acc, 445152000, 60198720)
    acc = mix2(acc, 843099768, 58919168)
    acc = acc % 2147483647
    acc = mix2(acc, 562627840, 618483968)
    acc = mix2(acc, 115977472, 842324288)
    acc = mix2(acc, 36843520, 436117560)
    acc = acc % 2147483647
    acc = mix2(acc, 160135168, 519194688)
    acc = mix2(acc, 548172544, 645342976)
    acc = mix2(acc, 412994304, 285843200)
    acc = acc % 2147483647
    acc = mix2(acc, 696967936, 323802880)
    acc = mix2(acc, 725203712, 546230016)
    acc = mix2(acc, 971002624, 801998592)
    acc = acc % 2147483647
    acc = mix2(acc, 597994240, 456589056)
    acc = mix2(acc, 303874880, 1027396224)
    acc = mix2(acc, 705220736, 407369216)
    acc = acc % 2147483647
    return acc
end

local function block69(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 127397440, 197665152)
    acc = mix2(acc, 661353408, 210041088)
    acc = mix2(acc, 779892032, 312237056)
    acc = acc % 2147483647
    acc = mix2(acc, 949705728, 405691392)
    acc = mix2(acc, 331701248, 890264576)
    acc = mix2(acc, 1006590976, 29514752)
    acc = acc % 2147483647
    acc = mix2(acc, 533289984, 180640768)
    acc = mix2(acc, 295918592, 815160320)
    acc = mix2(acc, 671964160, 129522688)
    acc = acc % 2147483647
    acc = mix2(acc, 495410176, 950688768)
    acc = mix2(acc, 814832640, 535583744)
    acc = mix2(acc, 61561856, 771644416)
    acc = acc % 2147483647
    acc = mix2(acc, 1029004288, 123755520)
    acc = mix2(acc, 672095232, 613571584)
    acc = mix2(acc, 307846144, 840195072)
    acc = acc % 2147483647
    acc = mix2(acc, 783506432, 812014592)
    acc = mix2(acc, 865950720, 336092160)
    acc = mix2(acc, 999775296, 386791168)
    acc = acc % 2147483647
    return acc
end

local function block70(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 473193280, 160991872)
    acc = mix2(acc, 791029760, 594491392)
    acc = mix2(acc, 158009344, 251828288)
    acc = acc % 2147483647
    acc = mix2(acc, 805208832, 222820096)
    acc = mix2(acc, 518413120, 727878272)
    acc = mix2(acc, 505948288, 644902592)
    acc = acc % 2147483647
    acc = mix2(acc, 136173568, 478488576)
    acc = mix2(acc, 652359680, 190973952)
    acc = mix2(acc, 657299520, 340624128)
    acc = acc % 2147483647
    acc = mix2(acc, 350942976, 156064512)
    acc = mix2(acc, 699002624, 506262272)
    acc = mix2(acc, 1026402048, 525486848)
    acc = acc % 2147483647
    acc = mix2(acc, 719593216, 1049846528)
    acc = mix2(acc, 518739968, 788834304)
    acc = mix2(acc, 539736064, 346916864)
    acc = acc % 2147483647
    acc = mix2(acc, 491984960, 256180992)
    acc = mix2(acc, 424441664, 224687616)
    acc = mix2(acc, 330984960, 132449792)
    acc = acc % 2147483647
    return acc
end

local function block71(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 139968064, 785625344)
    acc = mix2(acc, 370183424, 1040333120)
    acc = mix2(acc, 467931136, 1023082496)
    acc = acc % 2147483647
    acc = mix2(acc, 819933184, 799514624)
    acc = mix2(acc, 488591360, 280313856)
    acc = mix2(acc, 380399680, 52394752)
    acc = acc % 2147483647
    acc = mix2(acc, 232886072, 536689440)
    acc = mix2(acc, 563485824, 345592448)
    acc = mix2(acc, 191582400, 63705600)
    acc = acc % 2147483647
    acc = mix2(acc, 929028664, 127740416)
    acc = mix2(acc, 481759744, 762239488)
    acc = mix2(acc, 983499264, 877660672)
    acc = acc % 2147483647
    acc = mix2(acc, 622883328, 495761920)
    acc = mix2(acc, 295133696, 693168704)
    acc = mix2(acc, 743664896, 414947584)
    acc = acc % 2147483647
    acc = mix2(acc, 108942592, 1068523776)
    acc = mix2(acc, 564624640, 174483712)
    acc = mix2(acc, 395896064, 1071624512)
    acc = acc % 2147483647
    return acc
end

local function block72(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 834676736, 721500160)
    acc = mix2(acc, 1049954304, 1038391296)
    acc = mix2(acc, 279242752, 82245632)
    acc = acc % 2147483647
    acc = mix2(acc, 99076160, 70513408)
    acc = mix2(acc, 271099712, 532997760)
    acc = mix2(acc, 202462208, 586740800)
    acc = acc % 2147483647
    acc = mix2(acc, 669757184, 682341120)
    acc = mix2(acc, 329606912, 751766336)
    acc = mix2(acc, 487946752, 156312064)
    acc = acc % 2147483647
    acc = mix2(acc, 1005038144, 84094208)
    acc = mix2(acc, 672160000, 538187008)
    acc = mix2(acc, 83042560, 898841856)
    acc = acc % 2147483647
    acc = mix2(acc, 440871168, 348824896)
    acc = mix2(acc, 673320064, 558793344)
    acc = mix2(acc, 154189952, 661202624)
    acc = acc % 2147483647
    acc = mix2(acc, 169095168, 288923712)
    acc = mix2(acc, 124775296, 694711808)
    acc = mix2(acc, 963538432, 763954688)
    acc = acc % 2147483647
    return acc
end

local function block73(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 120307200, 360977920)
    acc = mix2(acc, 737250880, 347160832)
    acc = mix2(acc, 390520064, 780290304)
    acc = acc % 2147483647
    acc = mix2(acc, 901203200, 336498944)
    acc = mix2(acc, 543288576, 770185472)
    acc = mix2(acc, 426824960, 871424320)
    acc = acc % 2147483647
    acc = mix2(acc, 967940096, 1022224384)
    acc = mix2(acc, 503052288, 433637376)
    acc = mix2(acc, 1044207680, 847397632)
    acc = acc % 2147483647
    acc = mix2(acc, 65058560, 678435640)
    acc = mix2(acc, 626448128, 628865792)
    acc = mix2(acc, 178835200, 317870848)
    acc = acc % 2147483647
    acc = mix2(acc, 410486528, 436992)
    acc = mix2(acc, 217120569, 259406080)
    acc = mix2(acc, 817132864, 672111616)
    acc = acc % 2147483647
    acc = mix2(acc, 942513152, 955489280)
    acc = mix2(acc, 557947904, 242719744)
    acc = mix2(acc, 41262080, 161193016)
    acc = acc % 2147483647
    return acc
end

local function block74(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 204129312, 112685536)
    acc = mix2(acc, 873327776, 197570560)
    acc = mix2(acc, 758718528, 447113984)
    acc = acc % 2147483647
    acc = mix2(acc, 259374848, 794067776)
    acc = mix2(acc, 712610304, 205478400)
    acc = mix2(acc, 543523392, 912794880)
    acc = acc % 2147483647
    acc = mix2(acc, 60682496, 263048504)
    acc = mix2(acc, 316121344, 1054719296)
    acc = mix2(acc, 865289216, 745284608)
    acc = acc % 2147483647
    acc = mix2(acc, 889422848, 1018651648)
    acc = mix2(acc, 311280640, 806659136)
    acc = mix2(acc, 206937856, 283566912)
    acc = acc % 2147483647
    acc = mix2(acc, 252473856, 520665600)
    acc = mix2(acc, 996368960, 247497984)
    acc = mix2(acc, 243192064, 900541760)
    acc = acc % 2147483647
    acc = mix2(acc, 503706624, 283186240)
    acc = mix2(acc, 763449088, 539022080)
    acc = mix2(acc, 541329152, 555297536)
    acc = acc % 2147483647
    return acc
end

local function block75(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 80838400, 468009792)
    acc = mix2(acc, 1050366592, 172314624)
    acc = mix2(acc, 501432384, 685116160)
    acc = acc % 2147483647
    acc = mix2(acc, 477945600, 656220992)
    acc = mix2(acc, 195408384, 499710528)
    acc = mix2(acc, 944913792, 448880512)
    acc = acc % 2147483647
    acc = mix2(acc, 745462208, 470509312)
    acc = mix2(acc, 339544832, 962060032)
    acc = mix2(acc, 710681344, 769988352)
    acc = acc % 2147483647
    acc = mix2(acc, 562887424, 528551680)
    acc = mix2(acc, 377852672, 1000982272)
    acc = mix2(acc, 200010496, 923615040)
    acc = acc % 2147483647
    acc = mix2(acc, 627678720, 270300672)
    acc = mix2(acc, 202534400, 358417984)
    acc = mix2(acc, 100882688, 713757952)
    acc = acc % 2147483647
    acc = mix2(acc, 1000218880, 375536896)
    acc = mix2(acc, 610937152, 21315584)
    acc = mix2(acc, 603746360, 1073547264)
    acc = acc % 2147483647
    return acc
end

local function block76(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 120363008, 877455424)
    acc = mix2(acc, 352490240, 753059648)
    acc = mix2(acc, 281314816, 679558656)
    acc = acc % 2147483647
    acc = mix2(acc, 334206464, 117640704)
    acc = mix2(acc, 1049699904, 854915328)
    acc = mix2(acc, 741094656, 83411200)
    acc = acc % 2147483647
    acc = mix2(acc, 783834432, 812213248)
    acc = mix2(acc, 290964480, 941007872)
    acc = mix2(acc, 530318336, 379970560)
    acc = acc % 2147483647
    acc = mix2(acc, 135939072, 199697408)
    acc = mix2(acc, 375555072, 606757952)
    acc = mix2(acc, 437804800, 55747392)
    acc = acc % 2147483647
    acc = mix2(acc, 460141176, 867122496)
    acc = mix2(acc, 96912384, 492413952)
    acc = mix2(acc, 656606208, 757130240)
    acc = acc % 2147483647
    acc = mix2(acc, 343491584, 39461952)
    acc = mix2(acc, 15539968, 510157624)
    acc = mix2(acc, 905805568, 866206464)
    acc = acc % 2147483647
    return acc
end

local function block77(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 806598400, 69671680)
    acc = mix2(acc, 283160384, 814763648)
    acc = mix2(acc, 475831296, 855022656)
    acc = acc % 2147483647
    acc = mix2(acc, 807155456, 516266752)
    acc = mix2(acc, 828190528, 882036224)
    acc = mix2(acc, 826664448, 718740992)
    acc = acc % 2147483647
    acc = mix2(acc, 1018951168, 620490240)
    acc = mix2(acc, 365463040, 53820928)
    acc = mix2(acc, 177182208, 47648256)
    acc = acc % 2147483647
    acc = mix2(acc, 265627192, 917931520)
    acc = mix2(acc, 210451968, 942511616)
    acc = mix2(acc, 320671232, 295904832)
    acc = acc % 2147483647
    acc = mix2(acc, 228522240, 311309568)
    acc = mix2(acc, 951238976, 310245376)
    acc = mix2(acc, 288540672, 999249920)
    acc = acc % 2147483647
    acc = mix2(acc, 416851968, 124762176)
    acc = mix2(acc, 909452160, 980504064)
    acc = mix2(acc, 1006790144, 739876352)
    acc = acc % 2147483647
    return acc
end

local function block78(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 151811584, 708996672)
    acc = mix2(acc, 23109888, 221429048)
    acc = mix2(acc, 630313248, 193854080)
    acc = acc % 2147483647
    acc = mix2(acc, 751228928, 913975296)
    acc = mix2(acc, 439564288, 316606464)
    acc = mix2(acc, 761698304, 654239744)
    acc = acc % 2147483647
    acc = mix2(acc, 892925952, 349292544)
    acc = mix2(acc, 943872064, 748897024)
    acc = mix2(acc, 300791552, 435755776)
    acc = acc % 2147483647
    acc = mix2(acc, 390770432, 102675264)
    acc = mix2(acc, 422576768, 154419200)
    acc = mix2(acc, 381644864, 355891968)
    acc = acc % 2147483647
    acc = mix2(acc, 61640448, 1040913208)
    acc = mix2(acc, 534933248, 725316352)
    acc = mix2(acc, 928655104, 860146432)
    acc = acc % 2147483647
    acc = mix2(acc, 203726592, 235529024)
    acc = mix2(acc, 15954432, 6535680)
    acc = mix2(acc, 690119225, 782721024)
    acc = acc % 2147483647
    return acc
end

local function block79(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 478416896, 18546752)
    acc = mix2(acc, 605211392, 400203520)
    acc = mix2(acc, 1049092864, 419813376)
    acc = acc % 2147483647
    acc = mix2(acc, 1062021120, 638203904)
    acc = mix2(acc, 527042560, 513120256)
    acc = mix2(acc, 704768064, 420713216)
    acc = acc % 2147483647
    acc = mix2(acc, 371181312, 69856064)
    acc = mix2(acc, 1057413760, 729692160)
    acc = mix2(acc, 164497408, 589928512)
    acc = acc % 2147483647
    acc = mix2(acc, 647594752, 122091264)
    acc = mix2(acc, 41507648, 452561408)
    acc = mix2(acc, 4977216, 564516096)
    acc = acc % 2147483647
    acc = mix2(acc, 411164928, 131792192)
    acc = mix2(acc, 820479104, 401775232)
    acc = mix2(acc, 475985920, 335529984)
    acc = acc % 2147483647
    acc = mix2(acc, 584603712, 42025728)
    acc = mix2(acc, 91469568, 503782208)
    acc = mix2(acc, 114576000, 907470848)
    acc = acc % 2147483647
    return acc
end

local function block80(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 846264320, 900319232)
    acc = mix2(acc, 70932480, 598190080)
    acc = mix2(acc, 865253376, 965249024)
    acc = acc % 2147483647
    acc = mix2(acc, 143792128, 964978688)
    acc = mix2(acc, 84965376, 1016285248)
    acc = mix2(acc, 205091840, 768513088)
    acc = acc % 2147483647
    acc = mix2(acc, 463523584, 478827328)
    acc = mix2(acc, 801317376, 376547840)
    acc = mix2(acc, 741446208, 153810176)
    acc = acc % 2147483647
    acc = mix2(acc, 1005617408, 313859328)
    acc = mix2(acc, 334727488, 436412544)
    acc = mix2(acc, 708576960, 1042124800)
    acc = acc % 2147483647
    acc = mix2(acc, 343310336, 292859904)
    acc = mix2(acc, 474947648, 453274496)
    acc = mix2(acc, 1050867200, 374795776)
    acc = acc % 2147483647
    acc = mix2(acc, 327648832, 823908736)
    acc = mix2(acc, 505465728, 922211776)
    acc = mix2(acc, 1025946368, 100952064)
    acc = acc % 2147483647
    return acc
end

local function block81(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 395343936, 245818112)
    acc = mix2(acc, 674813760, 759780864)
    acc = mix2(acc, 611175936, 193259008)
    acc = acc % 2147483647
    acc = mix2(acc, 162143744, 647165504)
    acc = mix2(acc, 689604864, 202016000)
    acc = mix2(acc, 855691520, 487009536)
    acc = acc % 2147483647
    acc = mix2(acc, 432298240, 565712192)
    acc = mix2(acc, 559594496, 15350784)
    acc = mix2(acc, 704887808, 786381824)
    acc = acc % 2147483647
    acc = mix2(acc, 559201280, 710687744)
    acc = mix2(acc, 193838080, 44121152)
    acc = mix2(acc, 411195256, 83230272)
    acc = acc % 2147483647
    acc = mix2(acc, 901050624, 628673792)
    acc = mix2(acc, 283474176, 848475456)
    acc = mix2(acc, 724622336, 785767424)
    acc = acc % 2147483647
    acc = mix2(acc, 35052544, 46390328)
    acc = mix2(acc, 634258448, 307546880)
    acc = mix2(acc, 636788480, 731505408)
    acc = acc % 2147483647
    return acc
end

local function block82(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 95409920, 157334272)
    acc = mix2(acc, 422170432, 1064496768)
    acc = mix2(acc, 1067105280, 573193216)
    acc = acc % 2147483647
    acc = mix2(acc, 306428928, 304036864)
    acc = mix2(acc, 596950080, 288347904)
    acc = mix2(acc, 786479872, 279564032)
    acc = acc % 2147483647
    acc = mix2(acc, 794776320, 685803264)
    acc = mix2(acc, 985377536, 838991616)
    acc = mix2(acc, 546030336, 49035008)
    acc = acc % 2147483647
    acc = mix2(acc, 22387456, 145567544)
    acc = mix2(acc, 863001376, 1029356672)
    acc = mix2(acc, 269578880, 928051392)
    acc = acc % 2147483647
    acc = mix2(acc, 726741504, 449739264)
    acc = mix2(acc, 789979648, 382249472)
    acc = mix2(acc, 322736640, 289823296)
    acc = acc % 2147483647
    acc = mix2(acc, 401874304, 619560960)
    acc = mix2(acc, 700116992, 431513600)
    acc = mix2(acc, 549523456, 622067712)
    acc = acc % 2147483647
    return acc
end

local function block83(x: number, y: number): number
    local acc: number = x + y
    acc = mix2(acc, 562311168, 736665600)
    acc = mix2(acc, 315379712, 559824960)
    acc = mix2(acc, 506792704, 133160704)
    acc = acc % 2147483647
    acc = mix2(acc, 823403264, 440211200)
    acc = mix2(acc, 319093568, 825010816)
    acc = mix2(acc, 720881792, 496471552)
    acc = acc % 2147483647
    acc = mix2(acc, 449553984, 182286720)
    acc = mix2(acc, 340913152, 617196544)
    acc = mix2(acc, 608594944, 786705408)
    acc = acc % 2147483647
    acc = mix2(acc, 76475392, 79866944)
    acc = mix2(acc, 283051904, 809460224)
    acc = mix2(acc, 651749888, 497537536)
    acc = acc % 2147483647
    acc = mix2(acc, 779131456, 577854720)
    acc = mix2(acc, 580569344, 455460096)
    acc = mix2(acc, 790302976, 867462400)
    acc = acc % 2147483647
    acc = mix2(acc, 185112832, 658986304)
    acc = mix2(acc, 618200064, 892042240)
    acc = mix2(acc, 189529088, 505576448)
    acc = acc % 2147483647
    return acc
end

local function main(): number
    local total: number = 0
    total = bit32.bxor(total, block0(1047985216, 1047985217))
    total = bit32.bxor(total, block1(855971584, 855971585))
    total = bit32.bxor(total, block2(271383296, 271383297))
    total = bit32.bxor(total, block3(966856512, 966856513))
    total = bit32.bxor(total, block4(666447360, 666447361))
    total = bit32.bxor(total, block5(702334464, 702334465))
    total = bit32.bxor(total, block6(995393024, 995393025))
    total = bit32.bxor(total, block7(895342080, 895342081))
    total = bit32.bxor(total, block8(338185728, 338185729))
    total = bit32.bxor(total, block9(820864512, 820864513))
    total = bit32.bxor(total, block10(237542912, 237542913))
    total = bit32.bxor(total, block11(460289536, 460289537))
    total = bit32.bxor(total, block12(677998080, 677998081))
    total = bit32.bxor(total, block13(677905920, 677905921))
    total = bit32.bxor(total, block14(169786880, 169786881))
    total = bit32.bxor(total, block15(553326080, 553326081))
    total = bit32.bxor(total, block16(261918208, 261918209))
    total = bit32.bxor(total, block17(753706496, 753706497))
    total = bit32.bxor(total, block18(525786624, 525786625))
    total = bit32.bxor(total, block19(60864000, 60864001))
    total = bit32.bxor(total, block20(115453440, 115453441))
    total = bit32.bxor(total, block21(85673536, 85673537))
    total = bit32.bxor(total, block22(293902592, 293902593))
    total = bit32.bxor(total, block23(840127744, 840127745))
    total = bit32.bxor(total, block24(874602752, 874602753))
    total = bit32.bxor(total, block25(600079616, 600079617))
    total = bit32.bxor(total, block26(113207552, 113207553))
    total = bit32.bxor(total, block27(662562112, 662562113))
    total = bit32.bxor(total, block28(1005865984, 1005865985))
    total = bit32.bxor(total, block29(1043106816, 1043106817))
    total = bit32.bxor(total, block30(329239552, 329239553))
    total = bit32.bxor(total, block31(322047040, 322047041))
    total = bit32.bxor(total, block32(629860096, 629860097))
    total = bit32.bxor(total, block33(948341504, 948341505))
    total = bit32.bxor(total, block34(295919360, 295919361))
    total = bit32.bxor(total, block35(58827584, 58827585))
    total = bit32.bxor(total, block36(24130048, 24130049))
    total = bit32.bxor(total, block37(939620864, 939620865))
    total = bit32.bxor(total, block38(667755008, 667755009))
    total = bit32.bxor(total, block39(1051961856, 1051961857))
    total = bit32.bxor(total, block40(743150080, 743150081))
    total = bit32.bxor(total, block41(245979648, 245979649))
    total = bit32.bxor(total, block42(864518720, 864518721))
    total = bit32.bxor(total, block43(937567488, 937567489))
    total = bit32.bxor(total, block44(277868800, 277868801))
    total = bit32.bxor(total, block45(246926656, 246926657))
    total = bit32.bxor(total, block46(72094720, 72094721))
    total = bit32.bxor(total, block47(981251136, 981251137))
    total = bit32.bxor(total, block48(260337408, 260337409))
    total = bit32.bxor(total, block49(255161088, 255161089))
    total = bit32.bxor(total, block50(885794624, 885794625))
    total = bit32.bxor(total, block51(174220800, 174220801))
    total = bit32.bxor(total, block52(184131072, 184131073))
    total = bit32.bxor(total, block53(778860096, 778860097))
    total = bit32.bxor(total, block54(95815936, 95815937))
    total = bit32.bxor(total, block55(457180416, 457180417))
    total = bit32.bxor(total, block56(969429312, 969429313))
    total = bit32.bxor(total, block57(982272000, 982272001))
    total = bit32.bxor(total, block58(464181248, 464181249))
    total = bit32.bxor(total, block59(630073408, 630073409))
    total = bit32.bxor(total, block60(793432832, 793432833))
    total = bit32.bxor(total, block61(556140288, 556140289))
    total = bit32.bxor(total, block62(491989760, 491989761))
    total = bit32.bxor(total, block63(360939264, 360939265))
    total = bit32.bxor(total, block64(867123968, 867123969))
    total = bit32.bxor(total, block65(973715200, 973715201))
    total = bit32.bxor(total, block66(904207104, 904207105))
    total = bit32.bxor(total, block67(583446272, 583446273))
    total = bit32.bxor(total, block68(837464832, 837464833))
    total = bit32.bxor(total, block69(897083136, 897083137))
    total = bit32.bxor(total, block70(497421056, 497421057))
    total = bit32.bxor(total, block71(957144896, 957144897))
    total = bit32.bxor(total, block72(570473984, 570473985))
    total = bit32.bxor(total, block73(674567680, 674567681))
    total = bit32.bxor(total, block74(256806400, 256806401))
    total = bit32.bxor(total, block75(129410560, 129410561))
    total = bit32.bxor(total, block76(324591168, 324591169))
    total = bit32.bxor(total, block77(906907904, 906907905))
    total = bit32.bxor(total, block78(113683712, 113683713))
    total = bit32.bxor(total, block79(961403200, 961403201))
    total = bit32.bxor(total, block80(634859520, 634859521))
    total = bit32.bxor(total, block81(222273536, 222273537))
    total = bit32.bxor(total, block82(114774080, 114774081))
    total = bit32.bxor(total, block83(362863360, 362863361))
    return total
end

local function check(n: number): string
    if n % 2 == 0 then
        return "even"
    else
        return "odd"
    end
end

local result: number = main()
print("fixture ok", check(result), result % 1000000)
