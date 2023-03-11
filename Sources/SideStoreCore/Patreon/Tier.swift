//
//  Tier.swift
//  AltStore
//
//  Created by Riley Testut on 8/21/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

import Foundation

extension PatreonAPI {
    struct TierResponse: Decodable {
        struct Attributes: Decodable {
            var title: String
        }

        struct Relationships: Decodable {
            struct Benefits: Decodable {
                var data: [BenefitResponse]
            }

            var benefits: Benefits
        }

        var id: String
        var attributes: Attributes

        var relationships: Relationships
    }
}

public struct Tier {
    public var name: String
    public var identifier: String

    public var benefits: [Benefit] = []

    init(response: PatreonAPI.TierResponse) {
        name = response.attributes.title
        identifier = response.id
		benefits = response.relationships.benefits.data.compactMap(Benefit.init(response:))
	}
}
