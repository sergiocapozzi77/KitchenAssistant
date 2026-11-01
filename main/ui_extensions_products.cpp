#include "ui_extensions.h"

#include "actions.h"
#include "ProductsManager.h"
#include "screens.h"
#include "ui_theme.h"

#include "images.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <string>

namespace
{

const char *const kCategories[] = {
    "Baby", "Bakery", "Beverages", "Breakfast & Cereal", "Condiments & Dressing",
    "Cooking & Baking", "Dairy", "Deli", "Frozen Foods", "Grains",
    "Pasta & Sides", "Health & Personal Care", "Household & Cleaning", "Meat",
    "Pet Supplies", "Produce", "Seafood", "Snacks", "Soups & Canned Food",
    "Wine, Beer & Spirit", "Other"};
constexpr int kCategoryCount = 21;

ProductFilter s_filter = ProductFilter::All;
ProductSort s_sort = ProductSort::Name;
std::string s_search;
std::set<std::string> s_collapsed;

struct ModalState
{
    Product product;
    int quantity = 0;
};

ModalState *s_modal = nullptr;

struct GroupCtx
{
    lv_obj_t *content = nullptr;
    lv_obj_t *chevron = nullptr;
    std::string key;
};

std::string lower_copy(const std::string &value)
{
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c)
                   { return (char)std::tolower(c); });
    return out;
}

std::string trim_copy(const std::string &value)
{
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int days_until_expiry(const std::string &iso_date, bool frozen)
{
    if (iso_date.empty())
        return 9999;

    int year = 0, month = 0, day = 0;
    if (std::sscanf(iso_date.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
        return 9999;
    if (year < 1900 || year > 3000 || month < 1 || month > 12 ||
        day < 1 || day > 31)
        return 9999;

    std::tm expiry = {};
    expiry.tm_year = year - 1900;
    expiry.tm_mon = month - 1;
    expiry.tm_mday = day;

    if (frozen)
    {
        expiry.tm_mon += 4;
        if (expiry.tm_mon > 11)
        {
            expiry.tm_year += expiry.tm_mon / 12;
            expiry.tm_mon %= 12;
        }
    }

    std::time_t expiry_time = std::mktime(&expiry);
    if (expiry_time == (std::time_t)-1)
        return 9999;

    std::time_t now = std::time(nullptr);
    std::tm today = {};
    localtime_r(&now, &today);
    today.tm_hour = today.tm_min = today.tm_sec = 0;
    std::time_t midnight = std::mktime(&today);
    if (midnight == (std::time_t)-1)
        return 9999;

    return (int)std::round(std::difftime(expiry_time, midnight) / 86400.0);
}

bool valid_date(const std::string &value)
{
    if (value.size() != 10 || value[4] != '-' || value[7] != '-')
        return false;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (i == 4 || i == 7)
            continue;
        if (!std::isdigit((unsigned char)value[i]))
            return false;
    }

    int year = std::stoi(value.substr(0, 4));
    int month = std::stoi(value.substr(5, 2));
    int day = std::stoi(value.substr(8, 2));
    if (year < 1900 || year > 3000 || month < 1 || month > 12 ||
        day < 1 || day > 31)
        return false;

    std::tm tm_value = {};
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    std::time_t time_value = std::mktime(&tm_value);
    if (time_value == (std::time_t)-1)
        return false;

    std::tm round_trip = {};
    localtime_r(&time_value, &round_trip);
    return round_trip.tm_year == year - 1900 &&
           round_trip.tm_mon == month - 1 &&
           round_trip.tm_mday == day;
}

void add_expiry_badge(lv_obj_t *row, int days)
{
    if (days > 3)
        return;

    std::string text;
    uint32_t background = UI_COLOR_ERROR_SURFACE;
    uint32_t foreground = UI_COLOR_ERROR_TEXT;

    if (days < 0)
    {
        text = "Expired " + std::to_string(-days) + "d";
    }
    else if (days == 0)
    {
        text = "Today";
        background = UI_COLOR_TODAY_SURFACE;
        foreground = UI_COLOR_TODAY_TEXT;
    }
    else
    {
        text = std::to_string(days) + "d left";
        background = UI_COLOR_DAYSLEFT_SURFACE;
        foreground = UI_COLOR_DAYSLEFT_TEXT;
    }

    ui_theme::make_badge(row, text.c_str(), background, foreground);
}

void free_product_cb(lv_event_t *event)
{
    delete static_cast<Product *>(lv_event_get_user_data(event));
}

void free_string_cb(lv_event_t *event)
{
    delete static_cast<std::string *>(lv_event_get_user_data(event));
}

void edit_product_cb(lv_event_t *event)
{
    auto *product = static_cast<Product *>(lv_event_get_user_data(event));
    if (product)
        open_product_edit_modal(*product);
}

void delete_product_cb(lv_event_t *event)
{
    auto *id = static_cast<std::string *>(lv_event_get_user_data(event));
    if (id)
        request_product_delete(*id);
}

void group_toggle_cb(lv_event_t *event)
{
    auto *ctx = static_cast<GroupCtx *>(lv_event_get_user_data(event));
    if (!ctx || !ctx->content || !lv_obj_is_valid(ctx->content))
        return;

    bool is_collapsed = lv_obj_is_hidden(ctx->content);
    if (is_collapsed)
    {
        lv_obj_set_hidden(ctx->content, false);
        s_collapsed.erase(ctx->key);
        if (ctx->chevron)
            lv_label_set_text(ctx->chevron, LV_SYMBOL_UP);
    }
    else
    {
        lv_obj_set_hidden(ctx->content, true);
        s_collapsed.insert(ctx->key);
        if (ctx->chevron)
            lv_label_set_text(ctx->chevron, LV_SYMBOL_DOWN);
    }
}

void free_group_cb(lv_event_t *event)
{
    delete static_cast<GroupCtx *>(lv_event_get_user_data(event));
}

void add_empty_state(lv_obj_t *root, bool no_products)
{
    lv_obj_t *empty = lv_obj_create(root);
    lv_obj_set_width(empty, LV_PCT(100));
    lv_obj_set_height(empty, 320);
    lv_obj_set_style_bg_opa(empty, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(empty, 0, 0);
    lv_obj_set_style_pad_all(empty, 0, 0);
    lv_obj_set_style_pad_row(empty, 12, 0);
    lv_obj_set_flex_flow(empty, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_theme::make_label(empty,
                         no_products ? "No products yet" : "No matching products",
                         ui_theme::font_title, UI_COLOR_TEXT_PRIMARY);
    ui_theme::make_label(empty,
                         no_products
                             ? "Add items from the Kitchen Assistant app"
                             : "Change the filter or search text",
                         ui_theme::font_body, UI_COLOR_TEXT_MUTED);
}

lv_obj_t *make_product_row(lv_obj_t *parent, const Product &product, int days)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 96);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_PRODUCT_ROW), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 16, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(row, true);

    Product *row_product = new Product(product);
    lv_obj_add_event_cb(row, edit_product_cb, LV_EVENT_CLICKED, row_product);
    lv_obj_add_event_cb(row, free_product_cb, LV_EVENT_DELETE, row_product);

    lv_obj_t *name = ui_theme::make_label(
        row, product.name.c_str(), ui_theme::font_title, UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

    if (product.frozen)
    {
        lv_obj_t *snowflake = lv_image_create(row);
        lv_obj_set_size(snowflake, 40, 40);
        lv_image_set_src(snowflake, &img_snowflake);
        lv_obj_set_style_bg_opa(snowflake, LV_OPA_TRANSP, 0);
    }

    add_expiry_badge(row, days);

    lv_obj_t *edit = ui_theme::make_icon_button(row, LV_SYMBOL_EDIT, 56);
    Product *edit_product = new Product(product);
    lv_obj_add_event_cb(edit, edit_product_cb, LV_EVENT_CLICKED, edit_product);
    lv_obj_add_event_cb(edit, free_product_cb, LV_EVENT_DELETE, edit_product);

    lv_obj_t *remove = ui_theme::make_icon_button(
        row, LV_SYMBOL_TRASH, 56, UI_COLOR_ERROR_SURFACE);
    lv_obj_t *remove_icon = lv_obj_get_child(remove, 0);
    if (remove_icon)
        lv_obj_set_style_text_color(remove_icon,
                                    lv_color_hex(UI_COLOR_ERROR_TEXT), 0);
    std::string *delete_id = new std::string(product.id);
    lv_obj_add_event_cb(remove, delete_product_cb, LV_EVENT_CLICKED, delete_id);
    lv_obj_add_event_cb(remove, free_string_cb, LV_EVENT_DELETE, delete_id);

    return row;
}

void add_group(lv_obj_t *root, const std::string &category,
               const std::vector<Product> &products)
{
    std::string upper = category;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c)
                   { return (char)std::toupper(c); });

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 56);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(header, true);

    std::string title = upper + "  " LV_SYMBOL_BULLET "  " +
                        std::to_string(products.size()) +
                        (products.size() == 1 ? " item" : " items");
    ui_theme::make_label(header, title.c_str(), ui_theme::font_body,
                         UI_COLOR_TEXT_SECONDARY);

    lv_obj_t *chevron = ui_theme::make_label(
        header, LV_SYMBOL_UP, ui_theme::font_icon_small, UI_COLOR_TEXT_MUTED);

    lv_obj_t *content = lv_obj_create(root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    bool collapsed = s_collapsed.count(category) > 0;
    if (collapsed)
    {
        lv_obj_set_hidden(content, true);
        lv_label_set_text(chevron, LV_SYMBOL_DOWN);
    }

    auto *ctx = new GroupCtx{content, chevron, category};
    lv_obj_add_event_cb(header, group_toggle_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(header, free_group_cb, LV_EVENT_DELETE, ctx);

    for (const auto &product : products)
        make_product_row(content, product, days_until_expiry(product.expiry,
                                                             product.frozen));
}

void sync_filter_controls()
{
    if (!objects.product_filter_lbl || !lv_obj_is_valid(objects.product_filter_lbl))
        return;

    const char *filter_name = "All";
    if (s_filter == ProductFilter::ExpiringSoon)
        filter_name = "Expiring soon";
    else if (s_filter == ProductFilter::Expired)
        filter_name = "Expired";

    std::string filter_text = std::string("Filter: ") + filter_name;
    bool filter_active = s_filter != ProductFilter::All;
    if (filter_active)
        filter_text += "  " LV_SYMBOL_BULLET;
    lv_label_set_text(objects.product_filter_lbl, filter_text.c_str());
    lv_obj_set_style_text_color(
        objects.product_filter_lbl,
        lv_color_hex(filter_active ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_border_width(objects.product_filter_pill,
                                  filter_active ? 2 : 0, 0);
    lv_obj_set_style_border_color(objects.product_filter_pill,
                                  lv_color_hex(UI_COLOR_ACCENT), 0);

    lv_label_set_text(objects.product_sort_lbl,
                      s_sort == ProductSort::Name ? "Sort: A-Z"
                                                  : "Sort: By expiry");

    ui_theme::set_chip_active(objects.product_chip_all,
                              s_filter == ProductFilter::All);
    ui_theme::set_chip_active(objects.product_chip_expiring,
                              s_filter == ProductFilter::ExpiringSoon);
    ui_theme::set_chip_active(objects.product_chip_expired,
                              s_filter == ProductFilter::Expired);
    ui_theme::set_chip_active(objects.product_sort_chip_az,
                              s_sort == ProductSort::Name);
    ui_theme::set_chip_active(objects.product_sort_chip_expiry,
                              s_sort == ProductSort::Expiry);
}

} // namespace

void populateProductListUi(lv_obj_t *root, const std::vector<Product> &products)
{
    if (!root || !lv_obj_is_valid(root))
        return;

    lv_obj_clean(root);
    sync_filter_controls();

    std::string needle = lower_copy(s_search);
    bool use_search = s_search.size() >= 3;
    std::vector<Product> filtered;
    filtered.reserve(products.size());

    for (const auto &product : products)
    {
        int days = days_until_expiry(product.expiry, product.frozen);
        if (s_filter == ProductFilter::ExpiringSoon && !(days >= 0 && days <= 3))
            continue;
        if (s_filter == ProductFilter::Expired && !(days < 0))
            continue;
        if (use_search && lower_copy(product.name).find(needle) == std::string::npos)
            continue;

        Product copy = product;
        filtered.push_back(std::move(copy));
    }

    if (s_sort == ProductSort::Name)
    {
        std::stable_sort(filtered.begin(), filtered.end(),
                         [](const Product &a, const Product &b)
                         { return lower_copy(a.name) < lower_copy(b.name); });
    }
    else
    {
        std::stable_sort(filtered.begin(), filtered.end(),
                         [](const Product &a, const Product &b)
                         {
                             int da = days_until_expiry(a.expiry, a.frozen);
                             int db = days_until_expiry(b.expiry, b.frozen);
                             if (da != db)
                                 return da < db;
                             return lower_copy(a.name) < lower_copy(b.name);
                         });
    }

    if (filtered.empty())
    {
        add_empty_state(root, products.empty());
        return;
    }

    std::map<std::string, std::vector<Product>> groups;
    for (const auto &product : filtered)
        groups[product.category.empty() ? "Other" : product.category]
            .push_back(product);

    for (const auto &entry : groups)
        add_group(root, entry.first, entry.second);
}

void setProductSearchFilter(const std::string &filter)
{
    s_search = filter;
}

void setProductFilter(ProductFilter filter)
{
    s_filter = filter;
    sync_filter_controls();
    productsManager.populateProductList();
}

void setProductSort(ProductSort sort)
{
    s_sort = sort;
    sync_filter_controls();
    productsManager.populateProductList();
}

const char *product_category_options()
{
    static std::string options;
    if (options.empty())
    {
        for (int i = 0; i < kCategoryCount; ++i)
        {
            if (i > 0)
                options += "\n";
            options += kCategories[i];
        }
    }
    return options.c_str();
}

int product_category_index(const std::string &category)
{
    for (int i = 0; i < kCategoryCount; ++i)
    {
        if (category == kCategories[i])
            return i;
    }
    return kCategoryCount - 1;
}

std::string product_category_at(int index)
{
    if (index < 0 || index >= kCategoryCount)
        index = kCategoryCount - 1;
    return kCategories[index];
}

void open_product_edit_modal(const Product &product)
{
    delete s_modal;
    s_modal = new ModalState{product, product.quantity};

    if (!objects.product_edit_modal || !lv_obj_is_valid(objects.product_edit_modal))
        return;

    lv_textarea_set_text(objects.product_edit_name_ta, product.name.c_str());
    lv_textarea_set_text(objects.product_edit_expiry_ta, product.expiry.c_str());
    lv_dropdown_set_selected(objects.product_edit_category_dd,
                             product_category_index(product.category));
    if (objects.product_edit_frozen_cb)
    {
        if (product.frozen)
            lv_obj_add_state(objects.product_edit_frozen_cb, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(objects.product_edit_frozen_cb, LV_STATE_CHECKED);
    }
    adjust_product_modal_quantity(0);

    if (objects.keyboard)
        lv_obj_set_hidden(objects.keyboard, true);
    if (objects.product_edit_save_btn)
        lv_obj_clear_state(objects.product_edit_save_btn, LV_STATE_DISABLED);
    lv_obj_set_hidden(objects.product_edit_modal, false);
    lv_obj_move_foreground(objects.product_edit_modal);
}

void close_product_edit_modal()
{
    if (objects.product_edit_modal && lv_obj_is_valid(objects.product_edit_modal))
        lv_obj_set_hidden(objects.product_edit_modal, true);
    if (objects.keyboard)
        lv_obj_set_hidden(objects.keyboard, true);
    delete s_modal;
    s_modal = nullptr;
}

bool get_product_modal_values(Product &out)
{
    if (!s_modal)
        return false;

    std::string name = trim_copy(lv_textarea_get_text(objects.product_edit_name_ta));
    if (name.empty())
    {
        showSnackbar("Name is required", 3000);
        return false;
    }

    std::string expiry = trim_copy(lv_textarea_get_text(objects.product_edit_expiry_ta));
    if (!expiry.empty() && !valid_date(expiry))
    {
        showSnackbar("Use expiry YYYY-MM-DD", 3000);
        return false;
    }

    out = s_modal->product;
    out.name = name;
    out.expiry = expiry;
    out.category = product_category_at(lv_dropdown_get_selected(
        objects.product_edit_category_dd));
    out.quantity = s_modal->quantity;
    out.frozen = objects.product_edit_frozen_cb &&
                 lv_obj_has_state(objects.product_edit_frozen_cb, LV_STATE_CHECKED);
    return true;
}

bool product_modal_id(std::string &out)
{
    if (!s_modal)
        return false;
    out = s_modal->product.id;
    return !out.empty();
}

void adjust_product_modal_quantity(int delta)
{
    if (!s_modal)
        return;
    s_modal->quantity += delta;
    if (s_modal->quantity < 0)
        s_modal->quantity = 0;
    if (s_modal->quantity > 999)
        s_modal->quantity = 999;
    if (objects.product_edit_qty_lbl &&
        lv_obj_is_valid(objects.product_edit_qty_lbl))
        lv_label_set_text(objects.product_edit_qty_lbl,
                          std::to_string(s_modal->quantity).c_str());
}
