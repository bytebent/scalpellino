#pragma once
// The stars you can see with the naked eye (to magnitude 6.5), for Sky Lab's Earth star
// field. Brightest first. Data: Yale Bright Star Catalogue (see star_catalogue.cpp).

namespace scal {

struct CatalogueStar {
    float ra, dec;     // degrees (J2000)
    float magnitude;   // visual; smaller is brighter
    float kelvin;      // colour temperature
};

extern const CatalogueStar kStarCatalogue[];
extern const int kStarCatalogueSize;

} // namespace scal
