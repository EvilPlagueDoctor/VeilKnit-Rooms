#include <veilknit/veilknit.hpp>

#include <cassert>
#include <iostream>
#include <string>

int main() {
    using veilknit::Bytes;

    const auto parsed = veilknit::json::Value::parse(
        R"({"a":1,"b":[true,null,"hello\nworld"],"unicode":"\ud83d\ude80"})");
    assert(parsed.at("a").as_u64() == 1);
    assert(parsed.at("b").as_array().at(0).as_bool());
    assert(parsed.at("b").as_array().at(1).is_null());
    assert(parsed.at("unicode").as_string() == "🚀");
    assert(veilknit::json::Value::parse(parsed.dump()).at("a").as_u64() == 1);

    const Bytes binary{0, 1, 2, 3, 0xfe, 0xff};
    assert(veilknit::hex_decode(veilknit::hex_encode(binary)) == binary);
    assert(veilknit::base64_decode(veilknit::base64_encode(binary)) == binary);
    assert(veilknit::base64_encode(Bytes{'f'}) == "Zg==");
    assert(veilknit::base64_encode(Bytes{'f','o'}) == "Zm8=");
    assert(veilknit::base64_encode(Bytes{'f','o','o'}) == "Zm9v");

    assert(veilknit::to_wire(veilknit::RestrictionAction::ban) == "ban");
    assert(veilknit::to_wire(veilknit::ObservationKind::invalid_signature) == "InvalidSignature");
    assert(veilknit::default_capabilities().size() == 10);

    std::cout << "codec tests passed\n";
    return 0;
}
