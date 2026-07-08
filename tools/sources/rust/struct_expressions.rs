// Construction-site uses of structs/enums (struct_expression, field access)

#[derive(Debug, Clone)]
pub struct Address {
    pub street: String,
    pub city: String,
    pub zip: String,
}

#[derive(Debug)]
pub struct Person {
    pub name: String,
    pub age: u32,
    pub address: Address,
}

pub fn build_person(name: String) -> Person {
    let addr = Address {
        street: String::from("123 Main"),
        city: String::from("Springfield"),
        zip: String::from("00000"),
    };

    Person {
        name,
        age: 30,
        address: addr,
    }
}

pub fn update_with_spread(base: Person, new_age: u32) -> Person {
    Person {
        age: new_age,
        ..base
    }
}

pub fn access_fields(person: &Person) -> &str {
    let city = &person.address.city;
    let _name_len = person.name.len();
    city.as_str()
}

pub fn tuple_struct_use() {
    let point = (1, 2, 3);
    let (x, y, z) = point;
    let _ = x + y + z;
}
