#ifndef OPENMW_COMPONENTS_ESM4_TYPETRAITS
#define OPENMW_COMPONENTS_ESM4_TYPETRAITS

#include <type_traits>

namespace ESM4
{
    template <class T, class = std::void_t<>>
    struct HasFormId : std::false_type
    {
    };

    template <class T>
    struct HasFormId<T, std::void_t<decltype(T::mFormId)>> : std::true_type
    {
    };

    template <class T>
    inline constexpr bool hasFormId = HasFormId<T>::value;

    template <class T, class = std::void_t<>>
    struct HasId : std::false_type
    {
    };

    template <class T>
    struct HasId<T, std::void_t<decltype(T::mId)>> : std::true_type
    {
    };

    template <class T>
    inline constexpr bool hasId = HasId<T>::value;

    template <class T, class = std::void_t<>>
    struct HasFlags : std::false_type
    {
    };

    template <class T>
    struct HasFlags<T, std::void_t<decltype(T::mFlags)>> : std::true_type
    {
    };

    template <class T>
    inline constexpr bool hasFlags = HasFlags<T>::value;

    template <class T, class = std::void_t<>>
    struct HasEditorId : std::false_type
    {
    };

    template <class T>
    struct HasEditorId<T, std::void_t<decltype(T::mEditorId)>> : std::true_type
    {
    };

    template <class T>
    inline constexpr bool hasEditorId = HasEditorId<T>::value;

    template <class T, class = std::void_t<>>
    struct HasModel : std::false_type
    {
    };

    template <class T>
    struct HasModel<T, std::void_t<decltype(T::mModel)>> : std::true_type
    {
    };

    template <class T>
    inline constexpr bool hasModel = HasModel<T>::value;

    template <class T, class = std::void_t<>>
    struct HasNif : std::false_type
    {
    };

    template <class T>
    struct HasNif<T, std::void_t<decltype(T::mNif)>> : std::true_type
    {
    };

    template <class T>
    inline constexpr bool hasNif = HasNif<T>::value;

    template <class T, class = std::void_t<>>
    struct HasKf : std::false_type
    {
    };

    template <class T>
    struct HasKf<T, std::void_t<decltype(T::mKf)>> : std::true_type
    {
    };

    template <class T>
    inline constexpr bool hasKf = HasKf<T>::value;
}

#endif // OPENMW_COMPONENTS_ESM4_TYPETRAITS
