// SPDX-License-Identifier: GPL-3.0-or-later
#include "IconWords.h"

#include <QHash>
#include <QRegularExpression>
#include <utility>
#include <vector>

namespace lf::iconwords {

namespace {
// Russian word -> English words used in icon names (the first one is used
// for the Iconify search).
const std::vector<std::pair<const char*, const char*>>& dictionary()
{
    static const std::vector<std::pair<const char*, const char*>> d = {
        // packaging and handling
        {"коробка", "box,package"}, {"короб", "box,package"}, {"упаковка", "package,box"}, {"посылка", "package,parcel"},
        {"груз", "package,cargo"}, {"паллета", "pallet"}, {"поддон", "pallet"}, {"склад", "warehouse"},
        {"хрупкое", "glass,fragile"}, {"стекло", "glass"}, {"бокал", "glass,wine"}, {"зонт", "umbrella"},
        {"влага", "droplet,water"}, {"вода", "water,droplet"}, {"капля", "droplet,drop"}, {"верх", "arrow-up,up"},
        {"вверх", "arrow-up,up"}, {"вниз", "arrow-down,down"}, {"влево", "arrow-left,left"}, {"вправо", "arrow-right,right"},
        {"стрелка", "arrow"}, {"стрелки", "arrows,arrow"}, {"погрузчик", "forklift"}, {"тележка", "cart,trolley"},
        {"крюк", "hook"}, {"весы", "scale"}, {"вес", "weight,scale"}, {"гиря", "weight"},
        {"температура", "temperature,thermometer"}, {"термометр", "thermometer"}, {"холод", "snowflake,cold"},
        {"мороз", "snowflake"}, {"снежинка", "snowflake"}, {"жара", "sun,heat"}, {"солнце", "sun"},
        // transport
        {"доставка", "truck,delivery"}, {"грузовик", "truck"}, {"машина", "car"}, {"автомобиль", "car"},
        {"самолет", "plane,airplane"}, {"корабль", "ship"}, {"поезд", "train"}, {"велосипед", "bicycle,bike"},
        // safety
        {"опасность", "warning,danger,alert"}, {"внимание", "alert,warning"}, {"предупреждение", "warning,alert"},
        {"огонь", "fire,flame"}, {"пламя", "flame,fire"}, {"огнеопасно", "flame,fire"}, {"взрыв", "explosion,bomb"},
        {"яд", "skull,poison"}, {"череп", "skull"}, {"токсично", "skull,toxic"}, {"радиация", "radioactive,radiation"},
        {"биологическая", "biohazard"}, {"электричество", "bolt,electricity"}, {"молния", "bolt,lightning"},
        {"напряжение", "bolt,voltage"}, {"щит", "shield"}, {"защита", "shield,protection"}, {"замок", "lock"},
        {"ключ", "key"}, {"запрет", "ban,forbidden"}, {"нельзя", "ban,forbidden"}, {"курение", "smoking"},
        {"аптечка", "first-aid,medical"}, {"огнетушитель", "fire-extinguisher"}, {"выход", "exit,door"},
        {"дверь", "door"}, {"лестница", "stairs"}, {"перчатки", "glove"}, {"очки", "glasses"},
        // ecology
        {"переработка", "recycle"}, {"мусор", "trash"}, {"корзина", "trash,basket"}, {"урна", "trash"},
        {"лист", "leaf"}, {"дерево", "tree"}, {"цветок", "flower"}, {"растение", "plant"}, {"земля", "earth,world"},
        {"эко", "eco,leaf"},
        // shop and money
        {"магазин", "store,shop"}, {"покупка", "shopping,cart"}, {"корзинка", "basket,shopping"}, {"сумка", "bag"},
        {"пакет", "bag"}, {"подарок", "gift"}, {"скидка", "discount,percent"}, {"процент", "percent"},
        {"распродажа", "sale,discount"}, {"акция", "discount,sale"}, {"цена", "tag,price"}, {"ценник", "tag,price"},
        {"бирка", "tag,label"}, {"этикетка", "label,tag"}, {"деньги", "money,cash,coin"}, {"монета", "coin"},
        {"рубль", "ruble,currency"}, {"карта", "card,map"}, {"оплата", "payment,credit-card"}, {"штрихкод", "barcode"},
        {"новинка", "new,sparkle"}, {"новый", "new"}, {"звезда", "star"}, {"сердце", "heart"}, {"корона", "crown"},
        {"кубок", "trophy"}, {"медаль", "medal,award"}, {"гарантия", "shield-check,certificate"},
        // food
        {"еда", "food"}, {"продукты", "food"}, {"яблоко", "apple"}, {"фрукт", "fruit"}, {"овощ", "vegetable,carrot"},
        {"морковь", "carrot"}, {"хлеб", "bread"}, {"мясо", "meat"}, {"рыба", "fish"}, {"молоко", "milk"},
        {"сыр", "cheese"}, {"яйцо", "egg"}, {"кофе", "coffee"}, {"чай", "tea,cup"}, {"чашка", "cup,mug"},
        {"вино", "wine"}, {"пиво", "beer"}, {"бутылка", "bottle"}, {"торт", "cake"}, {"пицца", "pizza"},
        {"глютен", "gluten,wheat"}, {"пшеница", "wheat"}, {"орех", "nut,peanut"}, {"острый", "pepper,chili"},
        {"веган", "vegan,leaf"},
        // medicine
        {"лекарство", "pill,medicine"}, {"таблетка", "pill,tablet"}, {"шприц", "syringe"}, {"больница", "hospital"},
        {"врач", "doctor,stethoscope"}, {"здоровье", "health,heart"}, {"крест", "cross,plus"},
        // people and animals
        {"человек", "user,person"}, {"люди", "users,people"}, {"ребенок", "baby,child"}, {"дети", "child,baby"},
        {"инвалид", "wheelchair,accessible"}, {"туалет", "toilet,wc"}, {"собака", "dog"}, {"кошка", "cat"},
        {"животное", "animal,paw"}, {"лапа", "paw"}, {"рука", "hand"}, {"глаз", "eye"},
        // office and tech
        {"телефон", "phone"}, {"почта", "mail"}, {"письмо", "mail,letter"}, {"сайт", "world,web"}, {"интернет", "world,wifi"},
        {"дом", "home,house"}, {"здание", "building"}, {"завод", "factory"}, {"адрес", "map-pin,location"},
        {"место", "map-pin,location"}, {"флаг", "flag"}, {"календарь", "calendar"}, {"дата", "calendar"},
        {"часы", "clock"}, {"время", "clock,time"}, {"документ", "file,document"}, {"файл", "file"}, {"папка", "folder"},
        {"книга", "book"}, {"принтер", "printer"}, {"печать", "printer,stamp"}, {"камера", "camera"},
        {"фото", "photo,camera"}, {"музыка", "music"}, {"лампа", "bulb,lamp"}, {"свет", "bulb,light"},
        {"батарея", "battery"}, {"батарейка", "battery"}, {"вилка", "plug"}, {"розетка", "plug,socket"},
        {"настройки", "settings,gear"}, {"шестеренка", "gear,settings"}, {"инструмент", "tool,wrench"},
        {"молоток", "hammer"}, {"ножницы", "scissors"}, {"карандаш", "pencil"}, {"ручка", "pen"},
        {"кисть", "brush"}, {"линейка", "ruler"}, {"галочка", "check"}, {"отметка", "check"}, {"плюс", "plus"},
        {"минус", "minus"}, {"вопрос", "question"}, {"информация", "info"}, {"сообщение", "message,chat"},
        {"улыбка", "smile,mood"}, {"поиск", "search"}, {"лупа", "search,zoom"}, {"облако", "cloud"},
        {"дождь", "rain,cloud-rain"}, {"ветер", "wind"}, {"луна", "moon"}, {"круг", "circle"}, {"квадрат", "square"},
        {"треугольник", "triangle"}, {"ромб", "diamond,rhombus"}, {"шестиугольник", "hexagon"},
    };
    return d;
}

QString normalize(const QString& s)
{
    QString r = s.toLower();
    r.replace(QChar(0x0451), QChar(0x0435));  // ё -> е
    return r;
}

// "стрелка" -> "стрелк": a crude stem so that other endings still match
QString stem(const QString& word)
{
    static const QString vowels = QStringLiteral("аеиоуыэюяьй");
    if (word.size() > 4 && vowels.contains(word.back()))
        return word.left(word.size() - 1);
    return word;
}
} // namespace

bool hasCyrillic(const QString& s)
{
    for (const QChar c : s)
        if (c.script() == QChar::Script_Cyrillic)
            return true;
    return false;
}

QStringList translate(const QString& word)
{
    const QString w = normalize(word.trimmed());
    if (w.size() < 3 || !hasCyrillic(w))
        return {};
    QStringList out;
    for (const auto& [ru, en] : dictionary()) {
        const QString key = QString::fromUtf8(ru);
        // typing the beginning of a word, or a different ending of it
        if (key.startsWith(w) || w.startsWith(stem(key))) {
            for (const QString& e : QString::fromLatin1(en).split(QLatin1Char(',')))
                if (!out.contains(e))
                    out << e;
        }
    }
    return out;
}

QString englishQuery(const QString& query)
{
    QStringList words;
    for (const QString& w : query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        const QStringList t = hasCyrillic(w) ? translate(w) : QStringList();
        words << (t.isEmpty() ? w : t.first());
    }
    return words.join(QLatin1Char(' '));
}

Matcher::Matcher(const QString& query)
{
    m_words = query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString& w : m_words)
        m_translations << translate(w);
}

bool Matcher::operator()(const QString& name) const
{
    for (int i = 0; i < m_words.size(); ++i) {
        if (name.contains(m_words.at(i), Qt::CaseInsensitive))
            continue;
        bool found = false;
        for (const QString& t : m_translations.at(i))
            if (name.contains(t, Qt::CaseInsensitive)) {
                found = true;
                break;
            }
        if (!found)
            return false;
    }
    return true;
}

bool matches(const QString& name, const QString& query) { return Matcher(query)(name); }

} // namespace lf::iconwords
